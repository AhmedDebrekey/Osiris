#ifndef OSIRIS_SCRIPTTYPES_H
#define OSIRIS_SCRIPTTYPES_H

#include "rhi/RHITypes.h" // reuse the generic Handle<Tag> template, same as PhysicsTypes.h/AudioTypes.h

namespace Osiris {
    struct ScriptInstanceTag {};
    using ScriptInstanceHandle = Handle<ScriptInstanceTag>;
}

#endif //OSIRIS_SCRIPTTYPES_H
