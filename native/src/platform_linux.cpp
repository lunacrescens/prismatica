#include "platform.h"

bool platformDecodeFile(const std::string&, std::vector<float>&, int&, int&, std::string* err) {
  if (err) *err = "Unsupported audio format";
  return false;
}
bool platformSystemCaptureStart(CaptureCallback, void*, std::string* err) {
  if (err) *err = "System audio isn't supported here - pick a monitor/loopback device under Input instead";
  return false;
}
void platformSystemCaptureStop() {}
std::string platformSystemCaptureStatus() { return ""; }
