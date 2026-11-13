#ifndef AKIRA_INPUT_PAD_PATH_HPP
#define AKIRA_INPUT_PAD_PATH_HPP

#include <switch.h>

#include <vector>

namespace akira::input {

enum class PadPathKind {
    JoyCon,
    SwitchPro,
};

struct PadDescription {
    HidNpadIdType   npad  = HidNpadIdType_No1;
    PadPathKind     kind  = PadPathKind::JoyCon;
    HidNpadStyleTag style = HidNpadStyleTag_NpadFullKey;
    const char*     label = "";
};

std::vector<PadDescription> DescribePads();

} // namespace akira::input

#endif // AKIRA_INPUT_PAD_PATH_HPP
