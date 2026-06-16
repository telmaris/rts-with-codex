#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

struct Input
{
    int x;
};

// abstrakcja (pure virtual)
class InputHandler
{
    public:

    void Update(double dt);

    std::vector<Input> GetInputs() { return inputBuffer; }
    void AddInput(Input& in) { inputBuffer.push_back(in); }

    private:

    std::vector<Input> inputBuffer;
};


class LocalInputHandler
{

};

class AiInputHandler
{

};

class NetworkInputHandler
{

};

#endif