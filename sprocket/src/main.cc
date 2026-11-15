#include "korvid/frameloop/frameloop.h"

namespace {
constinit const kv::FrameLoopConfig k_config = {
    .title  = "Sprocket Rally",
    .width  = 1280,
    .height = 960,
    .exit_on_escape = false, // Escape pauses a race; on the title screen it quits
};
}

int main(int argc, char** argv) {
    return kv::frameloop_main(argc, argv, k_config);
}
