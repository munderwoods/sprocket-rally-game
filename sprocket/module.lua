module "sprocket" {
  output        = "exe",
  engine_module = true,
  deps   = { "core", "platform", "platform_hid", "math", "render", "sprite", "frameloop", "platform_audio", "asset", "krome" }, -- asset and krome: the tuning panel
  tools  = { ["shader-compile"] = { install = "shaders" } }, -- shaders/*.slang -> bin/shaders/ (the CRT shader)
}
