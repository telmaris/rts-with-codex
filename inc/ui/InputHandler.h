#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

// Placeholder input payload for future command-buffer based input.
struct Input
{
    int x{0};
};

// Base input buffer abstraction for local, AI or network command sources.
class InputHandler
{
    public:

    // Polls or produces inputs for one frame.
    void Update(double dt);

    // Returns the currently buffered inputs.
    std::vector<Input> GetInputs() { return inputBuffer; }
    // Appends one input command to the buffer.
    void AddInput(Input& in) { inputBuffer.push_back(in); }

    private:

    std::vector<Input> inputBuffer;
};

// Local player input source placeholder.
class LocalInputHandler
{

};

// AI input source placeholder.
class AiInputHandler
{

};

// Network input source placeholder.
class NetworkInputHandler
{

};

#endif
