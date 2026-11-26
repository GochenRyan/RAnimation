#include <Agent/ScriptBrain.h>

namespace RAnimation
{
    // Frame-indexed at 1/60 s (the timing assumes the UAL / animated_woman clip lengths):
    //   0.5 s walk, 1.5 s jog, 2.5 s jump, 5.0 s punch (upper body slot), 5.5 s crouch, 6.5 s stand still,
    //   7.0 s draw the pistol, 7.5 s hit (full body slot, locks movement), 9.0 s die.
    ScriptBrain::Script ScriptBrain::ControllerScript()
    {
        return [](int frame, DecisionContext&, AgentIntent& intent)
        {
            CharacterInput input;
            if (frame >= 30 && frame < 390)
            {
                input.moveY = 1.0f;
            }
            if (frame >= 90 && frame < 390)
            {
                input.run = true;
            }
            if (frame >= 330 && frame < 390)
            {
                input.crouch = true;
            }
            intent.direct = input;
            auto command = [&intent](CommandType type, const char* montage = "")
            {
                IntentCommand c;
                c.type = type;
                c.montage = montage;
                intent.commands.push_back(c);
            };
            if (frame == 150)
                command(CommandType::Jump);
            if (frame == 300)
                command(CommandType::Action, "punch");
            if (frame == 420)
                command(CommandType::ToggleWeapon);
            if (frame == 450)
                command(CommandType::Action, "hit");
            if (frame == 540)
                command(CommandType::Die);
        };
    }
} // namespace RAnimation
