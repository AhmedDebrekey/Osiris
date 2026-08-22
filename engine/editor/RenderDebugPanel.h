#ifndef OSIRIS_RENDERDEBUGPANEL_H
#define OSIRIS_RENDERDEBUGPANEL_H

namespace Osiris {
    class IRHI;

    class RenderDebugPanel {
    public:
        void Draw(IRHI* rhi, float deltaTime);

    private:
        int m_SelectedTexture = 0;
    };
}

#endif //OSIRIS_RENDERDEBUGPANEL_H
