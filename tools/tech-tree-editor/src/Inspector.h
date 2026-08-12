#ifndef INSPECTOR_H
#define INSPECTOR_H

// Field editor for the selected node. Every field of TechnologyDefinition that
// the .rtsdata format can express is editable here, so the file never has to be
// opened by hand.
//
// Text fields are edited as local buffers and pushed into the model on change;
// the buffers are refilled whenever the selection changes, so an in-progress
// edit is not clobbered by the model reformatting the value under the cursor.

#include "ui/UiWidgets.h"

#include "raylib.h"

#include <string>
#include <vector>

class TreeDocument;

class Inspector
{
public:
    // Draws the panel for `selectedId` (empty draws the "nothing selected"
    // hint). Returns the id the caller should select next — normally unchanged,
    // but a rename or delete redirects it.
    std::string Draw(Rectangle bounds, TreeDocument& document, const std::string& selectedId);

    // True while any inspector text field has focus, so the app can stop
    // single-key shortcuts from firing while you type.
    static bool IsEditing();

private:
    void SyncBuffers(TreeDocument& document, const std::string& id);

    // One row of modifier editors.
    struct ModifierRow
    {
        DropdownWidget stat;
        DropdownWidget building;
        DropdownWidget resource;
        DropdownWidget category;
        TextFieldWidget unit;
        TextFieldWidget additive;
        TextFieldWidget multiplier;
    };

    struct CostRow
    {
        DropdownWidget resource;
        TextFieldWidget amount;
    };

    std::string boundId;

    TextFieldWidget idField;
    TextFieldWidget nameField;
    TextFieldWidget descriptionField;
    TextFieldWidget laneField;
    TextFieldWidget researchTimeField;
    TextFieldWidget layerField;
    TextFieldWidget orderField;
    TextFieldWidget newPrerequisiteField;
    DropdownWidget categoryDropdown;
    std::vector<DropdownWidget> tagDropdowns;
    std::vector<CostRow> costRows;
    std::vector<DropdownWidget> buildingUnlockDropdowns;
    std::vector<ModifierRow> modifierRows;

    float scroll{0.0f};
    float maxScroll{0.0f};
};

#endif
