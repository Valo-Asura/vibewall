#include "core/wallhaven_client.hpp"

#include <cassert>
#include <iostream>

using namespace vibewall;

int main() {
  const std::string json = R"JSON(
  {
    "data": [
      {
        "id": "abc123",
        "url": "https://wallhaven.cc/w/abc123",
        "path": "https://w.wallhaven.cc/full/ab/wallhaven-abc123.jpg",
        "dimension_x": 1920,
        "dimension_y": 1080,
        "purity": "sfw",
        "category": "general",
        "colors": ["#112233", "#445566"],
        "thumbs": {
          "small": "https://th.wallhaven.cc/small/ab/abc123.jpg",
          "original": "https://th.wallhaven.cc/orig/ab/abc123.jpg"
        }
      }
    ]
  }
  )JSON";
  const auto entries = parse_wallhaven_response(json);
  assert(entries.size() == 1);
  assert(entries[0].id == "abc123");
  assert(entries[0].width == 1920);
  assert(entries[0].height == 1080);
  assert(entries[0].preview_url.find("abc123") != std::string::npos);
  assert(entries[0].colors_json.find("#112233") != std::string::npos);
  std::cout << "wallhaven tests ok\n";
  return 0;
}
