#pragma once

inline constexpr bool kShowAllProcessesDefault = false;

inline bool ShouldShowProcess(bool showAll, bool ownerKnown, bool ownedByCurrentUser) {
    return showAll || (ownerKnown && ownedByCurrentUser);
}
