#ifndef PILOT_SYSTEM_H
#define PILOT_SYSTEM_H

class PilotSystem {
public:
    static void init(int argc, char **argv);
    static void finalize();

private:
    static void bootstrapInPathBmts();
};

#endif
