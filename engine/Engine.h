#ifndef OSIRIS_ENGINE_H
#define OSIRIS_ENGINE_H

namespace Osiris {
    class Engine {
    public:
        Engine();
        ~Engine();

        Engine(const Engine &)=delete;
        Engine &operator=(const Engine &)=delete;

        void Initialize();
        void Run();
        void Shutdown();
    };
}

#endif