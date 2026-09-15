// Read-only SDL identity probe for the pinned Dolphin controller backend.
// This intentionally does not read serials/paths or send input/rumble.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace
{
constexpr std::string_view kSchema = "webmelee-controller-probe";
constexpr int kVersion = 1;
constexpr int kMaxSiChannels = 4;
constexpr int kWiiUAdapter = 12;

struct Config
{
  bool supplied = false;
  bool gc_adapter_configured = false;
  std::map<std::string, std::string> hints;
};

struct Device
{
  std::string name;
  int index = 0;
  SDL_JoystickID instance_id = 0;
  Uint16 vendor_id = 0;
  Uint16 product_id = 0;
  std::string guid;
  bool virtual_device = false;
};

std::string Trim(std::string value)
{
  const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

bool ParseInteger(std::string_view value, int* result)
{
  if (value.empty())
    return false;
  std::size_t consumed = 0;
  try
  {
    const int parsed = std::stoi(std::string(value), &consumed, 10);
    if (consumed != value.size())
      return false;
    *result = parsed;
    return true;
  }
  catch (...)
  {
    return false;
  }
}

std::optional<Config> ReadConfig(const fs::path& path, std::string* error)
{
  std::ifstream stream(path);
  if (!stream)
  {
    *error = "cannot read Dolphin configuration";
    return std::nullopt;
  }

  Config config;
  config.supplied = true;
  std::string section;
  std::string line;
  int si_devices[kMaxSiChannels] = {};
  bool seen_si[kMaxSiChannels] = {};
  while (std::getline(stream, line))
  {
    line = Trim(line);
    if (line.empty() || line.front() == '#' || line.front() == ';')
      continue;
    if (line.front() == '[' && line.back() == ']')
    {
      section = Trim(line.substr(1, line.size() - 2));
      continue;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos)
    {
      *error = "malformed Dolphin configuration line";
      return std::nullopt;
    }
    const std::string key = Trim(line.substr(0, equals));
    const std::string value = Trim(line.substr(equals + 1));
    if (section == "SDL_Hints")
    {
      if (key.empty())
      {
        *error = "empty SDL hint name";
        return std::nullopt;
      }
      config.hints[key] = value;
    }
    else if (section == "Core" && key.rfind("SIDevice", 0) == 0)
    {
      int index = -1;
      if (!ParseInteger(key.substr(8), &index) || index < 0 || index >= kMaxSiChannels)
      {
        *error = "invalid SIDevice key";
        return std::nullopt;
      }
      int device = 0;
      if (!ParseInteger(value, &device))
      {
        *error = "invalid SIDevice value";
        return std::nullopt;
      }
      si_devices[index] = device;
      seen_si[index] = true;
    }
  }
  if (!stream.eof())
  {
    *error = "error while reading Dolphin configuration";
    return std::nullopt;
  }
  for (int index = 0; index < kMaxSiChannels; ++index)
    config.gc_adapter_configured |= seen_si[index] && si_devices[index] == kWiiUAdapter;
  return config;
}

bool SetHint(const char* name, const char* value, std::string* error)
{
  if (!SDL_SetHint(name, value))
  {
    *error = std::string("SDL_SetHint failed for ") + name + ": " + SDL_GetError();
    return false;
  }
  return true;
}

bool ApplyDolphinHints(const Config& config, std::string* error)
{
  // This ordering mirrors Dolphin's SDL backend. Explicit values from the
  // [SDL_Hints] section are applied last, including an explicit GC hint.
  const auto configured_or = [&config](const char* name, const char* fallback) {
    const auto it = config.hints.find(name);
    return it == config.hints.end() || it->second.empty() ? std::string(fallback) : it->second;
  };
  const struct DefaultHint
  {
    const char* name;
    const char* value;
  } defaults[] = {
      {SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1"},
      {SDL_HINT_JOYSTICK_WGI, "0"},
      {SDL_HINT_JOYSTICK_HIDAPI_PS5_PLAYER_LED, "0"},
      {SDL_HINT_JOYSTICK_DIRECTINPUT, "1"},
      {SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, "1"},
      {SDL_HINT_JOYSTICK_HIDAPI_VERTICAL_JOY_CONS, "0"},
  };
  for (const auto& hint : defaults)
  {
    const std::string value = configured_or(hint.name, hint.value);
    if (!SetHint(hint.name, value.c_str(), error))
      return false;
  }
  if (!SetHint(SDL_HINT_JOYSTICK_HIDAPI_GAMECUBE, config.gc_adapter_configured ? "0" : "1",
               error))
    return false;
  for (const auto& [name, value] : config.hints)
  {
    if (!SetHint(name.c_str(), value.c_str(), error))
      return false;
  }
  return true;
}

std::string JsonEscape(std::string_view value)
{
  std::ostringstream output;
  for (const unsigned char ch : value)
  {
    switch (ch)
    {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (ch < 0x20)
      {
        output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(ch) << std::dec << std::setfill(' ');
      }
      else
      {
        output << static_cast<char>(ch);
      }
      break;
    }
  }
  return output.str();
}

std::string JsonString(std::string_view value)
{
  return "\"" + JsonEscape(value) + "\"";
}

void PrintError(std::string_view message)
{
  std::cout << "{\"schema\":" << JsonString(kSchema) << ",\"version\":" << kVersion
            << ",\"status\":\"error\",\"error\":" << JsonString(message) << "}\n";
}

std::string GuidString(SDL_GUID guid)
{
  char text[33] = {};
  SDL_GUIDToString(guid, text, sizeof(text));
  return text;
}

std::optional<std::vector<Device>> Enumerate(std::string* error)
{
  int count = 0;
  SDL_JoystickID* const ids = SDL_GetJoysticks(&count);
  if (ids == nullptr)
  {
    *error = std::string("SDL_GetJoysticks failed: ") + SDL_GetError();
    return std::nullopt;
  }

  std::vector<Device> devices;
  std::map<std::string, int> next_index;
  for (int input_index = 0; input_index < count; ++input_index)
  {
    const SDL_JoystickID instance_id = ids[input_index];
    SDL_Gamepad* const gamepad = SDL_OpenGamepad(instance_id);
    SDL_Joystick* const joystick = SDL_OpenJoystick(instance_id);
    if (joystick == nullptr)
    {
      if (gamepad != nullptr)
        SDL_CloseGamepad(gamepad);
      continue;
    }

    // Match Dolphin's invalid-HID guard in OpenAndAddDevice. The remaining
    // Gamepad constructor operations only describe controls; this probe never
    // reads state and never creates an output/haptic object.
    const int buttons = SDL_GetNumJoystickButtons(joystick);
    const int axes = SDL_GetNumJoystickAxes(joystick);
    const int hats = SDL_GetNumJoystickHats(joystick);
    const int balls = SDL_GetNumJoystickBalls(joystick);
    const bool valid = buttons >= 0 && axes >= 0 && hats >= 0 && balls >= 0 && buttons <= 255 &&
                       axes <= 255 && hats <= 255 && balls <= 255;
    if (!valid)
    {
      if (gamepad != nullptr)
        SDL_CloseGamepad(gamepad);
      SDL_CloseJoystick(joystick);
      continue;
    }

    const char* const name = gamepad != nullptr ? SDL_GetGamepadName(gamepad)
                                                 : SDL_GetJoystickName(joystick);
    Device device;
    device.name = name != nullptr ? name : "Unknown";
    device.index = next_index[device.name]++;
    device.instance_id = instance_id;
    device.vendor_id = SDL_GetJoystickVendor(joystick);
    device.product_id = SDL_GetJoystickProduct(joystick);
    device.guid = GuidString(SDL_GetJoystickGUID(joystick));
    device.virtual_device = SDL_IsJoystickVirtual(instance_id);
    devices.push_back(std::move(device));

    // The close order follows Dolphin's Gamepad destructor.
    if (gamepad != nullptr)
      SDL_CloseGamepad(gamepad);
    SDL_CloseJoystick(joystick);
  }
  SDL_free(ids);
  return devices;
}

void PrintSuccess(const Config& config, const std::vector<Device>& devices)
{
  std::cout << "{\"schema\":" << JsonString(kSchema) << ",\"version\":" << kVersion
            << ",\"status\":\"ok\",\"config\":{\"provided\":"
            << (config.supplied ? "true" : "false") << ",\"gc_adapter_configured\":"
            << (config.gc_adapter_configured ? "true" : "false")
            << ",\"gc_adapter_hint\":"
            << JsonString(config.gc_adapter_configured ? "0" : "1") << "},\"devices\":[";
  for (std::size_t i = 0; i < devices.size(); ++i)
  {
    const Device& device = devices[i];
    if (i != 0)
      std::cout << ',';
    std::cout << "{\"name\":" << JsonString(device.name) << ",\"index\":" << device.index
              << ",\"instance_id\":" << device.instance_id << ",\"vendor_id\":"
              << device.vendor_id << ",\"product_id\":" << device.product_id
              << ",\"guid\":" << JsonString(device.guid) << ",\"virtual\":"
              << (device.virtual_device ? "true" : "false") << '}';
  }
  std::cout << "]}\n";
}
}  // namespace

int main(int argc, char** argv)
{
  std::optional<fs::path> config_path;
  for (int i = 1; i < argc; ++i)
  {
    const std::string_view argument = argv[i];
    if (argument == "--config" && i + 1 < argc)
    {
      config_path = fs::path(argv[++i]);
    }
    else if (argument == "--help")
    {
      std::cout << "usage: webmelee-controller-probe [--config Dolphin.ini]\n";
      return 0;
    }
    else
    {
      PrintError("unknown or incomplete argument");
      return 2;
    }
  }

  Config config;
  std::string error;
  if (config_path)
  {
    const auto parsed = ReadConfig(*config_path, &error);
    if (!parsed)
    {
      PrintError(error);
      return 1;
    }
    config = *parsed;
  }
  if (!ApplyDolphinHints(config, &error))
  {
    PrintError(error);
    return 1;
  }

  if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMEPAD))
  {
    PrintError(std::string("SDL_Init failed: ") + SDL_GetError());
    return 1;
  }
  const auto devices = Enumerate(&error);
  SDL_Quit();
  if (!devices)
  {
    PrintError(error);
    return 1;
  }
  PrintSuccess(config, *devices);
  return 0;
}
