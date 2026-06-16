#ifndef GUI_H
#define GUI_H

#include "Utils.h"

// TODO: napisać klase UiCanvas która przechowuje elementy UI i zarządza nimi (przede wszystkim rozmiarem)

class UiWidget
{
public:
    virtual void Update(double dt) = 0;

    inline void ChangePosition(int x, int y)
    {
        pos.x = x;
        pos.y = y;
    }
    inline void ChangeSize(int sizeX, int sizeY)
    {
        size.x = sizeX;
        size.y = sizeY;
    }

    inline void ChangePositionAnchor(Vec2f anchor)
    {
        posAnchor = anchor;
    }
    inline void ChangeSizeAnchor(Vec2f anchor)
    {
        sizeAnchor = anchor;
    }

    inline void UpdateSize(Vec2i windowSize)
    {
        pos = Vec2i{windowSize.x * posAnchor.x, windowSize.y * posAnchor.y};
        size = Vec2i{windowSize.x * sizeAnchor.x, windowSize.y * sizeAnchor.y};
    }

    Vec2i pos{100, 100};
    Vec2i size{200, 100};

    Vec2f posAnchor{0.2f, 0.2f};
    Vec2f sizeAnchor{0.2f, 0.1f};

    // std::string name{"default widget name"};
    // int id{0};
};

class UiButton : public UiWidget
{
public:
    void Update(double dt) override;

    inline void ChangeText(std::string stryng)
    {
        text = stryng;
    }

    virtual void OnClick() { func(); }

    std::string text{"Default button text"};
    std::function<void()> func;
    // int number;
};

class CheckBox : public UiWidget
{
public:
    CheckBox()
    {
        size = Vec2i{30, 30};
        sizeAnchor = Vec2f{0.05f, 0.05f};
    }

    void Update(double dt) override;

    inline void ChangeText(std::string stryng)
    {
        text = stryng;
    }

    inline bool IsActive() { return currentState; }
    inline bool HasChanged()
    {
        if (previousState != currentState)
        {
            previousState = currentState;
            return true;
        }
        return false;
    }

    bool currentState{false};
    bool previousState{false};

    std::string text{"Default checkbox text"};
};

class SliderBar : public UiWidget
{
public:
    SliderBar()
    {
        size = Vec2i{30, 30};
        sizeAnchor = Vec2f{0.4f, 0.05f};
    }

    void Update(double dt) override;

    inline void ChangeText(std::string stryng)
    {
        text = stryng;
    }

    float GetValue()
    {
        return currentValue;
    }

    inline bool HasChanged()
    {
        if (previousValue != currentValue)
        {
            previousValue = currentValue;
            return true;
        }
        return false;
    }

    float currentValue;
    float previousValue;
    std::string text{"Default slider bar text"};
};

class VBox : public UiWidget
{
public:
    void Update(double dt) override;

    inline void UpdateSize(Vec2i windowSize)
    {
        pos = Vec2i{windowSize.x * posAnchor.x, windowSize.y * posAnchor.y};
        size = Vec2i{windowSize.x * sizeAnchor.x, windowSize.y * sizeAnchor.y};

        int childrenCount = children.size();
        if(childrenCount == 0) return;

        Vec2i childrenSize{size.x, size.y / childrenCount};

        for (int i = 0; i < childrenCount; i++)
        {
            children[i]->size = childrenSize;
            children[i]->pos = Vec2i{pos.x, pos.y + (childrenSize.y + margins.y) * i};
        }
    }

    void UpdateSize()
    {
        int childrenCount = children.size();
        Vec2i childrenSize{size.x, size.y / childrenCount};

        for (int i = 0; i < childrenCount; i++)
        {
            children[i]->size = childrenSize;
            children[i]->pos = Vec2i{pos.x, pos.y + (childrenSize.y + margins.y) * i};
        }
    }

    void AddChild(std::shared_ptr<UiWidget> child)
    {
        children.push_back(child);
        UpdateSize();
    }

    std::vector<std::shared_ptr<UiWidget>> children;

    Vec4i margins{0, 5, 0, 0}; // up down left right
};

class HBox : public UiWidget
{
public:
    void Update(double dt) override;

    inline void UpdateSize(Vec2i windowSize)
    {
        pos = Vec2i{windowSize.x * posAnchor.x, windowSize.y * posAnchor.y};
        size = Vec2i{windowSize.x * sizeAnchor.x, windowSize.y * sizeAnchor.y};

        int childrenCount = children.size();
        Vec2i childrenSize{size.x / childrenCount, size.y};

        for (int i = 0; i < childrenCount; i++)
        {
            children[i]->size = childrenSize;
            children[i]->pos = Vec2i{pos.x + (childrenSize.x + margins.x) * i, pos.y};
        }
    }

    void UpdateSize()
    {
        int childrenCount = children.size();
        Vec2i childrenSize{size.x / childrenCount, size.y};

        for (int i = 0; i < childrenCount; i++)
        {
            children[i]->size = childrenSize;
            children[i]->pos = Vec2i{pos.x + (childrenSize.x + margins.x) * i, pos.y};
        }
    }

    void AddChild(std::shared_ptr<UiWidget> child)
    {
        children.push_back(child);
        UpdateSize();
    }

    std::vector<std::shared_ptr<UiWidget>> children;

    Vec4i margins{0, 5, 0, 0}; // up down left right
};

class TextBox : public UiWidget
{
    public:

    void Update(double dt) override;

    inline void ChangeText(std::string stryng)
    {
        text = stryng;
    }

    inline bool HasChanged()
    {
        if (text.compare(textOutput))
        {
            text = textOutput;
            return true;
        }
        return false;
    }

    std::string GetText() { return text; }

    char textOutput[16] = "\0";
    std::string text{"Default textbox text"};
};

//TODO:
class DropdownBox : public UiWidget
{
    public:

    void Update(double dt) override;

    inline void ChangeText(std::string stryng)
    {
        text = stryng;
    }

    inline bool HasChanged()
    {
        if (text.compare(textOutput))
        {
            text = textOutput;
            return true;
        }
        return false;
    }

    std::string GetText() { return text; }

    char* textOutput;
    std::string text{"Default dropdown text"};
};


class GuiPanel : public UiWidget
{
    public:
        void Update(double dt) override;

        std::string text{"Gui Panel"};
};
#endif