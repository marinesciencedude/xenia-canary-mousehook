project_root = "../../../.."
include(project_root.."/tools/build")

group("src")
project("xenia-hid-mousehook")
  uuid("cee8ea9d-c0a9-4ff1-b851-1e63bb46738b")
  kind("StaticLib")
  language("C++")
  links({
    "xenia-base",
    "xenia-hid",
	"xenia-ui",
	"xenia-kernel"
  })
  defines({
  })
  local_platform_files()
