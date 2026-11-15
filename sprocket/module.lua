module "sprocket" {
  output        = "exe",
  engine_module = true,
  deps   = { "core", "platform", "platform_hid", "math", "render", "sprite", "frameloop", "platform_audio" },
  tools  = { ["shader-compile"] = { install = "shaders" } }, -- shaders/*.slang -> bin/shaders/ (the CRT shader)
}
