#include "input/pad_path.hpp"

namespace akira::input {

std::vector<PadDescription> DescribePads()
{
    static constexpr HidNpadIdType scanOrder[] = {
        HidNpadIdType_Handheld,
        HidNpadIdType_No1,
        HidNpadIdType_No2,
        HidNpadIdType_No3,
        HidNpadIdType_No4,
    };

    std::vector<PadDescription> out;

    for (HidNpadIdType npad : scanOrder) {
        PadState probe{};
        padInitialize(&probe, npad);
        padUpdate(&probe);

        if (!padIsConnected(&probe))
            continue;

        const uint64_t styleSet = padGetStyleSet(&probe);

        PadDescription desc;
        desc.npad = npad;

        if (styleSet & HidNpadStyleTag_NpadHandheld) {
            desc.kind  = PadPathKind::JoyCon;
            desc.style = HidNpadStyleTag_NpadHandheld;
            desc.label = "Handheld";
        } else if (styleSet & HidNpadStyleTag_NpadFullKey) {
            desc.kind  = PadPathKind::SwitchPro;
            desc.style = HidNpadStyleTag_NpadFullKey;
            desc.label = "Controller";
        } else if (styleSet & HidNpadStyleTag_NpadJoyDual) {
            desc.kind  = PadPathKind::JoyCon;
            desc.style = HidNpadStyleTag_NpadJoyDual;
            desc.label = "Joy-Con (L/R)";
        } else {
            continue;
        }

        out.push_back(desc);
    }

    return out;
}

} // namespace akira::input
