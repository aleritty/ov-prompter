#include <algorithm>
#include <array>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <openvino/genai/llm_pipeline.hpp>
#include <openvino/genai/visual_language/pipeline.hpp>
#include <openvino/runtime/tensor.hpp>

namespace fs = std::filesystem;

struct Config {
  std::string model_path;
  std::string device_list;
  std::string system_prompt;
};

struct PipelineHandle {
  std::unique_ptr<ov::genai::LLMPipeline> llm;
  std::unique_ptr<ov::genai::VLMPipeline> vlm;

  bool valid() const {
    return static_cast<bool>(llm) || static_cast<bool>(vlm);
  }
};

bool looks_like_vlm_model(const fs::path &model_path) {
  return fs::exists(model_path / "openvino_language_model.xml") &&
         (fs::exists(model_path / "openvino_vision_embeddings_model.xml") ||
          fs::exists(model_path / "openvino_text_embeddings_model.xml"));
}

std::string generate_with_pipeline(PipelineHandle &pipeline,
                                   const std::string &prompt,
                                   const ov::genai::GenerationConfig &config) {
  if (pipeline.vlm) {
    auto results = pipeline.vlm->generate(prompt, std::vector<ov::Tensor>{},
                                          config, std::monostate{});
    if (results.texts.empty()) {
      throw std::runtime_error("VLM pipeline returned no text output");
    }
    return results.texts.front();
  }

  if (!pipeline.llm) {
    throw std::runtime_error("Pipeline handle is empty");
  }

  return pipeline.llm->generate(prompt, config);
}

static constexpr int kMaxVerbosity = 3;
static constexpr int kMaxNewTokens = 256;
static constexpr int kExitInterrupt = 130;

ov::genai::GenerationConfig build_generation_config() {
  ov::genai::GenerationConfig config;
  config.max_new_tokens = kMaxNewTokens;
  config.do_sample = false;
  config.repetition_penalty = 1.1f;
  config.no_repeat_ngram_size = 4;
  return config;
}

static volatile sig_atomic_t g_interrupted = 0;

static void signal_handler(int /*signal*/) { g_interrupted = 1; }

static bool check_interrupt() { return g_interrupted != 0; }

PipelineHandle
initialize_pipeline(const fs::path &model_path,
                    const std::vector<std::string> &device_candidates,
                    std::string &active_device) {
  PipelineHandle pipeline;
  std::exception_ptr last_error;
  const bool prefers_vlm = looks_like_vlm_model(model_path);

  for (const auto &candidate : device_candidates) {
    if (check_interrupt()) {
      throw std::runtime_error("Interrupted during device initialization");
    }

    try {
      if (prefers_vlm) {
        pipeline.vlm =
            std::make_unique<ov::genai::VLMPipeline>(model_path, candidate);
      } else {
        pipeline.llm =
            std::make_unique<ov::genai::LLMPipeline>(model_path, candidate);
      }
      active_device = candidate;
      return pipeline;
    } catch (const std::exception &e) {
      last_error = std::current_exception();
      std::cerr << "Warning: failed to initialize device '" << candidate
                << "': " << e.what() << '\n';

      // If LLM failed, try VLM as fallback
      if (!prefers_vlm) {
        try {
          pipeline.vlm =
              std::make_unique<ov::genai::VLMPipeline>(model_path, candidate);
          pipeline.llm.reset();
          active_device = candidate;
          return pipeline;
        } catch (...) {
          pipeline.vlm.reset();
        }
      }
    } catch (...) {
      last_error = std::current_exception();
      std::cerr << "Warning: failed to initialize device '" << candidate
                << "' (unknown error)\n";
    }
  }

  // No device succeeded
  if (last_error) {
    std::rethrow_exception(last_error);
  }
  throw std::runtime_error("Failed to initialize any device candidate");
}

std::string build_prompt(const std::string &question, int verbosity_level,
                         const std::string &system_prompt) {
  static constexpr std::array<std::string_view, 4> instructions = {
      "Provide ONLY the bash command itself with no comments, explanations, or "
      "extra text. ",
      "Provide the bash command, then add a single line starting with '# "
      "Summary:' that concisely states what it does. ",
      "Provide the bash command, then add '# Summary:' and '# Detail:' lines "
      "explaining intent and key flags or assumptions. Keep explanations under "
      "three sentences total. ",
      "Provide the bash command followed by '# Summary:', '# Detail:', and '# "
      "Pitfall:' lines giving intent, execution details, and one potential "
      "caveat or alternative. Keep each line short. "};

  const int clamped_level =
      std::clamp(verbosity_level, 0, static_cast<int>(instructions.size()) - 1);

  std::ostringstream prompt;
  if (system_prompt.empty()) {
    prompt << "You are a bash command expert. " << instructions[clamped_level]
           << "User request: " << question << "\n";
  } else {
    prompt << system_prompt;
    if (system_prompt.back() != '\n') {
      prompt << '\n';
    }
    prompt << "User request: " << question << "\n";
  }

  switch (clamped_level) {
  case 0:
    prompt << "Command:";
    break;
  case 1:
    prompt << "Command and summary:";
    break;
  case 2:
    prompt << "Command, summary, and detail:";
    break;
  default:
    prompt << "Command, summary, detail, and pitfall:";
    break;
  }

  return prompt.str();
}

std::string get_config_path() {
  const char *home = std::getenv("HOME");
  if (!home) {
    throw std::runtime_error("HOME environment variable not set");
  }

  std::string home_str(home);
  if (home_str.find("..") != std::string::npos || home_str.empty() ||
      home_str[0] != '/') {
    throw std::runtime_error("Invalid HOME environment variable");
  }

  return home_str + "/.config/ov-prompter";
}

Config read_config() {
  Config config;
  std::ifstream file(get_config_path());
  if (!file.is_open()) {
    return config;
  }

  std::string line;
  while (std::getline(file, line)) {
    // Trim trailing whitespace and Windows line endings
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                             line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }

    if (line.empty()) {
      continue;
    }
    if (line.rfind("model=", 0) == 0) {
      config.model_path = line.substr(6);
    } else if (line.rfind("device=", 0) == 0) {
      config.device_list = line.substr(7);
    } else if (line.rfind("system_prompt=", 0) == 0) {
      config.system_prompt = line.substr(14);
    } else if (config.model_path.empty()) {
      config.model_path = line;
    } else if (config.device_list.empty()) {
      config.device_list = line;
    } else if (config.system_prompt.empty()) {
      config.system_prompt = line;
    }
  }

  return config;
}

std::vector<std::string> parse_device_list(const std::string &device_arg) {
  std::vector<std::string> devices;
  std::stringstream ss(device_arg);
  std::string item;
  while (std::getline(ss, item, ',')) {
    auto start = item.find_first_not_of(" \t");
    auto end = item.find_last_not_of(" \t");
    if (start == std::string::npos || end == std::string::npos) {
      continue;
    }
    devices.emplace_back(item.substr(start, end - start + 1));
  }
  if (devices.empty()) {
    std::cerr << "Warning: Empty device list, falling back to CPU\n";
    devices.emplace_back("CPU");
  }
  return devices;
}

std::string join_device_list(const std::vector<std::string> &devices) {
  std::ostringstream joined;
  for (size_t i = 0; i < devices.size(); ++i) {
    if (i > 0) {
      joined << ',';
    }
    joined << devices[i];
  }
  return joined.str();
}

void write_config(const Config &config) {
  std::string config_path = get_config_path();
  fs::path config_dir = fs::path(config_path).parent_path();

  if (!fs::exists(config_dir)) {
    std::error_code ec;
    fs::create_directories(config_dir, ec);
    if (ec) {
      throw std::runtime_error("Failed to create config directory: " +
                               ec.message());
    }
  }

  // Check if directory is writable
  auto perms = fs::status(config_dir).permissions();
  if ((perms & fs::perms::owner_write) == fs::perms::none) {
    throw std::runtime_error("Config directory is not writable: " +
                             config_dir.string());
  }

  std::ofstream file(config_path);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to create config file: " + config_path);
  }

  if (!config.model_path.empty()) {
    file << "model=" << config.model_path << '\n';
  }
  if (!config.device_list.empty()) {
    file << "device=" << config.device_list << '\n';
  }
  if (!config.system_prompt.empty()) {
    file << "system_prompt=" << config.system_prompt << '\n';
  }
}

void print_usage(const char *prog) {
  std::cerr
      << "Usage:\n"
      << "  " << prog << " --question \"<question>\" [options]\n"
      << "  " << prog << " --set-model-default <model_path>\n"
      << "  " << prog << " --set-device-default <device_list>\n"
      << "  " << prog << " --set-system-prompt-default <prompt>\n"
      << "\n"
      << "Options:\n"
      << "  --question <text>           Question to ask the model (required)\n"
      << "  --model-path <path>         Path to the model (overrides default)\n"
      << "  --set-model-default <path>  Persist default model path to config\n"
      << "  --set-device-default <list> Persist default device priority list\n"
      << "  --set-system-prompt-default <prompt> Persist default system "
         "prompt\n"
      << "  --device <list>             Device priority, e.g. GPU,CPU "
         "(default: GPU,CPU)\n"
      << "  --system-prompt <prompt>    Override system prompt for this run\n"
      << "  --show-system-prompt        Print the current persisted system "
         "prompt\n"
      << "  -v / -vv / -vvv             Increase output verbosity (0-3)\n"
      << "  --verbose                   Same as -v\n"
      << "  --verbosity <0-3>           Set verbosity level explicitly\n"
      << "\n"
      << "Environment:\n"
      << "  OPENVINO_DEVICE             Override default device list\n"
      << "\n"
      << "Examples:\n"
      << "  " << prog << " --set-model-default ./models/Phi-4-mini\n"
      << "  " << prog << " --set-device-default GPU,CPU\n"
      << "  " << prog << " --question \"List all files recursively\"\n"
      << "  " << prog << " -vv --question \"Find large files\" --device CPU\n";
}

int main(int argc, char *argv[]) {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  std::ios_base::sync_with_stdio(false);

  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  std::string model_path;
  std::string question;
  std::vector<std::string> device_candidates;
  int verbosity_level = 0;
  bool user_specified_device = false;
  bool set_model_default = false;
  bool set_device_default = false;
  bool set_system_prompt_default = false;
  bool show_system_prompt = false;
  std::string default_device_arg;
  std::string system_prompt_override;
  std::string system_prompt_default;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--set-model-default") {
      if (i + 1 >= argc) {
        std::cerr << "Error: --set-model-default requires a path argument\n";
        return 1;
      }
      set_model_default = true;
      model_path = argv[++i];
    } else if (arg == "--set-device-default") {
      if (i + 1 >= argc) {
        std::cerr << "Error: --set-device-default requires a value (e.g., CPU "
                     "or GPU,CPU)\n";
        return 1;
      }
      set_device_default = true;
      default_device_arg = join_device_list(parse_device_list(argv[++i]));
    } else if (arg == "--set-system-prompt-default") {
      if (i + 1 >= argc) {
        std::cerr << "Error: --set-system-prompt-default requires a prompt\n";
        return 1;
      }
      set_system_prompt_default = true;
      system_prompt_default = argv[++i];
    } else if (arg == "--model-path") {
      if (i + 1 >= argc) {
        std::cerr << "Error: --model-path requires a path argument\n";
        return 1;
      }
      model_path = argv[++i];
    } else if (arg == "--question") {
      if (i + 1 >= argc) {
        std::cerr << "Error: --question requires a text argument\n";
        return 1;
      }
      question = argv[++i];
    } else if (arg == "--device") {
      if (i + 1 >= argc) {
        std::cerr << "Error: --device requires a value (e.g., CPU, GPU, or "
                     "GPU,CPU)\n";
        return 1;
      }
      device_candidates = parse_device_list(argv[++i]);
      user_specified_device = true;
    } else if (arg == "--system-prompt") {
      if (i + 1 >= argc) {
        std::cerr << "Error: --system-prompt requires a prompt string\n";
        return 1;
      }
      std::string prompt_arg = argv[++i];

      // Validate system prompt length
      constexpr size_t kMaxSystemPromptLength = 4096;
      if (prompt_arg.length() > kMaxSystemPromptLength) {
        std::cerr << "Error: System prompt exceeds maximum length of "
                  << kMaxSystemPromptLength << " characters\n";
        return 1;
      }

      // Check for null bytes
      if (prompt_arg.find('\0') != std::string::npos) {
        std::cerr << "Error: System prompt contains invalid characters\n";
        return 1;
      }

      system_prompt_override = prompt_arg;
    } else if (arg == "--show-system-prompt") {
      show_system_prompt = true;
    } else if (arg == "--verbose") {
      verbosity_level = std::min(kMaxVerbosity, verbosity_level + 1);
    } else if (arg == "--verbosity") {
      if (i + 1 >= argc) {
        std::cerr << "Error: --verbosity requires a numeric value\n";
        return 1;
      }
      try {
        verbosity_level = std::clamp(std::stoi(argv[++i]), 0, kMaxVerbosity);
      } catch (const std::exception &) {
        std::cerr << "Error: --verbosity expects an integer between 0 and 3\n";
        return 1;
      }
    } else {
      const bool is_short_v_flag =
          arg.size() > 1 && arg[0] == '-' &&
          arg.find_first_not_of('v', 1) == std::string::npos;
      if (is_short_v_flag) {
        // Prevent overflow from extremely long -vvvvv... arguments
        constexpr size_t kMaxVFlagLength = 100;
        if (arg.size() > kMaxVFlagLength) {
          std::cerr << "Error: Verbosity flag too long (max " << kMaxVFlagLength
                    << " characters)\n";
          return 1;
        }
        const int increment = static_cast<int>(arg.size()) - 1;
        verbosity_level = std::min(kMaxVerbosity, verbosity_level + increment);
      } else {
        std::cerr << "Error: Unknown argument: " << arg << '\n';
        print_usage(argv[0]);
        return 1;
      }
    }
  }

  Config persisted = read_config();

  if (set_model_default || set_device_default || set_system_prompt_default) {
    try {
      Config updated = persisted;
      if (set_model_default) {
        // Validate model path exists and is a directory
        if (!fs::exists(model_path)) {
          throw std::runtime_error("Model path does not exist: " + model_path);
        }
        if (!fs::is_directory(model_path)) {
          throw std::runtime_error("Model path is not a directory: " +
                                   model_path);
        }

        std::error_code ec;
        fs::path abs_path = fs::absolute(model_path, ec);
        if (ec) {
          throw std::runtime_error("Failed to resolve model path: " +
                                   ec.message());
        }

        // Ensure the resolved path is still the same directory (no symlink
        // escapes)
        if (!fs::exists(abs_path) || !fs::is_directory(abs_path)) {
          throw std::runtime_error("Resolved model path is invalid");
        }

        updated.model_path = abs_path.string();
      }
      if (set_device_default) {
        updated.device_list = default_device_arg;
      }
      if (set_system_prompt_default) {
        // Validate system prompt length and content
        constexpr size_t kMaxSystemPromptLength = 4096;
        if (system_prompt_default.length() > kMaxSystemPromptLength) {
          throw std::runtime_error("System prompt exceeds maximum length of " +
                                   std::to_string(kMaxSystemPromptLength) +
                                   " characters");
        }

        // Check for null bytes (potential injection)
        if (system_prompt_default.find('\0') != std::string::npos) {
          throw std::runtime_error("System prompt contains invalid null bytes");
        }

        updated.system_prompt = system_prompt_default;
      }
      write_config(updated);
      if (set_model_default) {
        std::cerr << "Default model set to: " << updated.model_path << '\n';
      }
      if (set_device_default) {
        std::cerr << "Default device list set to: " << updated.device_list
                  << '\n';
      }
      if (set_system_prompt_default) {
        std::cerr << "Default system prompt set." << '\n';
      }
      return 0;
    } catch (const std::exception &e) {
      std::cerr << "Error: " << e.what() << '\n';
      return 1;
    }
  }

  if (show_system_prompt) {
    if (persisted.system_prompt.empty()) {
      std::cout << "(System prompt not set; using built-in default.)" << '\n';
    } else {
      std::cout << persisted.system_prompt << '\n';
    }
    return 0;
  }

  if (question.empty()) {
    std::cerr << "Error: --question is required\n";
    print_usage(argv[0]);
    return 1;
  }

  // Check for whitespace-only questions
  if (question.find_first_not_of(" \t\n\r") == std::string::npos) {
    std::cerr << "Error: --question cannot be empty or whitespace-only\n";
    return 1;
  }

  if (model_path.empty()) {
    model_path = persisted.model_path;
    if (model_path.empty()) {
      std::cerr
          << "Error: No model path specified and no default configured\n"
          << "Use --model-path or set a default with --set-model-default\n";
      return 1;
    }
  }

  if (!user_specified_device) {
    const char *env_device = std::getenv("OPENVINO_DEVICE");
    if (env_device && std::strlen(env_device) > 0) {
      device_candidates = parse_device_list(env_device);
    } else if (!persisted.device_list.empty()) {
      device_candidates = parse_device_list(persisted.device_list);
    } else {
      device_candidates = {"GPU", "CPU"};
    }
  }

  const std::string active_system_prompt = system_prompt_override.empty()
                                               ? persisted.system_prompt
                                               : system_prompt_override;

  try {
    if (check_interrupt())
      return kExitInterrupt;

    const fs::path model_dir(model_path);
    std::string active_device;
    PipelineHandle pipeline =
        initialize_pipeline(model_dir, device_candidates, active_device);

    if (!pipeline.valid()) {
      throw std::runtime_error("Failed to initialize pipeline");
    }

    if (active_device != device_candidates.front()) {
      std::cerr << "Info: fell back to device '" << active_device << "'\n";
    }

    if (check_interrupt())
      return kExitInterrupt;

    const std::string prompt =
        build_prompt(question, verbosity_level, active_system_prompt);

    auto config = build_generation_config();
    const std::string result = generate_with_pipeline(pipeline, prompt, config);

    if (check_interrupt())
      return kExitInterrupt;

    std::cout << result << '\n';
    return 0;
  } catch (const std::exception &e) {
    if (check_interrupt())
      return kExitInterrupt;
    std::cerr << "Error: " << e.what() << '\n';
    return 1;
  }
}
