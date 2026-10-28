// The five dropdowns: the firmware set, and the three whose value is an operator
// parameter (program, variation, left, right). A pick is sent once, and the box
// keeps it until the snapshot carries it.
//
// The rule this pins down: JUCE delivers ComboBox::onChange asynchronously
// (ComboBox::sendChange -> triggerAsyncUpdate, and a popup pick goes through
// ModalComponentManager first), so a handler that reads getSelectedId() there
// names the box's id when the notification lands. The editor's 20 Hz refresh
// rewrote that id in between -- a boot or a program load takes seconds, and the
// snapshot names what the engine runs -- so picking another set or program
// either sent nothing, was undone on the next tick, or (with no guard at all)
// sent the running one back. The tick reads the box now, before it writes it: a
// selection the editor did not draw is the user's.
//
//   editor_dropdown_select      (no ROMs, no audio, no window)

#include "../../editor/editor_view.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
    std::printf("  %s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok) {
        failures++;
    }
}

// The editor's calls, recorded.
struct FakeCommands final : lexui::UiCommands {
    std::vector<int> sets;
    std::vector<int> programs;
    std::vector<int> variations;
    std::vector<std::pair<int, int>> pairs;
    int others = 0;

    void addRomLocation(const std::string &) override {}
    void selectRomSet(int index) override {
        sets.push_back(index);
    }
    void selectProgram(int index) override {
        programs.push_back(index);
    }
    void selectVariation(int variation) override {
        variations.push_back(variation);
    }
    void moveSlider(int, int) override {
        others++;
    }
    void setToggle(const std::string &, bool) override {
        others++;
    }
    void setLevelDb(float) override {
        others++;
    }
    void setDryWet(float) override {
        others++;
    }
    void setOutputPair(int left, int right) override {
        pairs.emplace_back(left, right);
    }
    void setAnalog(bool) override {
        others++;
    }
    void gesture(lexui::UiGesture, int, bool) override {}
};

// Three programs of three variations, and no pages: the dropdowns need labels
// and variation lists only.
lexcat::Catalog testCatalog() {
    lexcat::Catalog catalog;
    catalog.version = 1;
    catalog.rom = "0123456789abcdef";
    catalog.remote = lexcat::Remote::Larc;
    for (int i = 1; i <= 3; i++) {
        lexcat::Program program;
        program.bank = 1;
        program.program = i;
        program.name = "P" + std::to_string(i);
        program.key = "1." + std::to_string(i);
        program.where = "B1 P" + std::to_string(i);
        program.group = "1 TEST";
        program.label = "PROGRAM " + std::to_string(i) + " (B1 P" + std::to_string(i) + ")";
        program.variations = {1, 2, 3};
        catalog.programs.push_back(program);
    }
    return catalog;
}

void setRom(lexui::UiSnapshot &s, const std::string &rom) {
    std::snprintf(s.romHash, sizeof s.romHash, "%s", rom.c_str());
}

// The firmware list the processor publishes: name, and whether it can be loaded.
void nameSet(lexui::UiSnapshot &s, int index, const char *name, bool unsupported = false) {
    std::snprintf(s.romSets[index].name, sizeof s.romSets[index].name, "%s", name);
    s.romSets[index].model = 0;
    s.romSets[index].unsupported = unsupported;
}

}  // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    lexcat::CatalogLibrary catalogs;
    catalogs.catalogs.push_back(testCatalog());
    const std::string rom = catalogs.catalogs[0].rom;
    lexcat::ParamHelp help;
    FakeCommands commands;
    lexui::UiSnapshot snapshot = lexui::defaultSnapshot();
    setRom(snapshot, rom);
    snapshot.status = lexui::UiStatus::Ready;
    snapshot.program = 1;             // PROGRAM 2 runs (a program's id is its index + 1)
    snapshot.requestedProgram = 1;
    snapshot.variation = 1;           // V1 runs (a variation's id is its number)
    snapshot.requestedVariation = 1;
    snapshot.outLeft = 0;             // left A (an output's id is the DAC + 1)
    snapshot.outRight = 2;            // right C
    snapshot.romSetCount = 3;         // the firmware list (a set's id is its index + 1)
    nameSet(snapshot, 0, "224 v3.2");
    nameSet(snapshot, 1, "224 v4.3");
    nameSet(snapshot, 2, "224 v4.4");
    snapshot.selectedRomSet = 1;       // 224 v4.3 runs
    snapshot.pendingRomSet = -1;

    lexui::EditorView view(catalogs, help, [&snapshot](lexui::UiSnapshot &s) { s = snapshot; }, commands);
    view.refresh();
    juce::ComboBox &program = view.programBox();
    juce::ComboBox &variation = view.variationBox();
    juce::ComboBox &left = view.outputBox(0);
    juce::ComboBox &right = view.outputBox(1);
    juce::ComboBox &firmware = view.romSetBox();

    std::printf("the boxes show what runs, and send nothing\n");
    check(firmware.getSelectedId() == 2, "firmware id 2 (224 v4.3)");
    check(program.getSelectedId() == 2, "program id 2 (PROGRAM 2)");
    check(variation.getSelectedId() == 1, "variation id 1 (V1)");
    check(left.getSelectedId() == 1 && right.getSelectedId() == 3, "left A, right C");
    check(commands.sets.empty() && commands.programs.empty() && commands.variations.empty() && commands.pairs.empty(),
          "no command");
    check(commands.others == 0, "no other command");

    std::printf("a click in the popup on PROGRAM 3 (id 3)\n");
    program.setSelectedId(3);   // exactly what the popup's result callback does
    std::printf("the tick that still sees PROGRAM 2 running\n");
    view.refresh();
    check(commands.programs.size() == 1 && commands.programs[0] == 2, "one command, for index 2");
    check(program.getSelectedId() == 3, "the choice is not undone");

    std::printf("further ticks while it loads\n");
    view.refresh();
    check(commands.programs.size() == 1, "still one command");
    check(program.getSelectedId() == 3, "the choice is kept");

    std::printf("the processor reports the request (the parameter took it)\n");
    snapshot.requestedProgram = 2;
    view.refresh();
    check(commands.programs.size() == 1, "still one command");
    check(program.getSelectedId() == 3, "the box shows the request");

    std::printf("its machine takes it\n");
    snapshot.program = 2;
    view.refresh();
    check(commands.programs.size() == 1, "still one command");
    check(program.getSelectedId() == 3, "the box shows it");

    std::printf("150 ms of the message loop: the editor's own ticks, and any stale notification\n");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    check(commands.programs.size() == 1, "still one command");
    check(program.getSelectedId() == 3, "still PROGRAM 3");

    std::printf("a pick whose load never lands\n");
    program.setSelectedId(1);   // PROGRAM 1, index 0
    view.refresh();
    check(commands.programs.size() == 2 && commands.programs[1] == 0, "one command, for index 0");
    snapshot.requestedProgram = 0;   // the parameter took it
    view.refresh();
    check(program.getSelectedId() == 1, "the box shows the request");
    snapshot.requestedProgram = 2;   // ... and the write-back put PROGRAM 3 back
    view.refresh();
    check(commands.programs.size() == 2, "no command for the failure");
    check(program.getSelectedId() == 3, "the box is back on the running program");

    std::printf("no program to name (the machine stopped)\n");
    snapshot.program = -1;
    view.refresh();
    check(program.getSelectedId() == 0, "the box is empty, as it was before the fix");
    snapshot.program = 2;
    view.refresh();
    check(program.getSelectedId() == 3, "and back on the running program");

    std::printf("a click in the popup on V3 while V1 runs\n");
    variation.setSelectedId(3);
    view.refresh();
    check(commands.variations.size() == 1 && commands.variations[0] == 3, "one command, for V3");
    check(variation.getSelectedId() == 3, "the choice is not undone");
    snapshot.requestedVariation = 3;
    view.refresh();
    check(commands.variations.size() == 1, "still one command");
    check(variation.getSelectedId() == 3, "the box shows the request");
    snapshot.variation = 3;
    view.refresh();
    check(commands.variations.size() == 1, "still one command");
    check(variation.getSelectedId() == 3, "the box shows it");

    std::printf("a click in the popup on left C\n");
    left.setSelectedId(3);
    view.refresh();
    check(commands.pairs.size() == 1 && commands.pairs[0] == std::make_pair(2, 2), "one command, left C and right C");
    check(left.getSelectedId() == 3, "the choice is not undone");
    snapshot.outLeft = 2;   // the parameter took it
    view.refresh();
    check(commands.pairs.size() == 1, "still one command");
    check(left.getSelectedId() == 3, "the box shows it");

    std::printf("a click in the popup on right A\n");
    right.setSelectedId(1);
    view.refresh();
    check(commands.pairs.size() == 2 && commands.pairs[1] == std::make_pair(2, 0), "one command, left C and right A");
    check(right.getSelectedId() == 1, "the choice is not undone");
    snapshot.outRight = 0;
    view.refresh();
    check(commands.pairs.size() == 2, "still one command");
    check(right.getSelectedId() == 1, "the box shows it");

    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    check(commands.programs.size() == 2 && commands.variations.size() == 1 && commands.pairs.size() == 2,
          "no command from a notification");

    std::printf("a click in the popup on 224 v4.4 (id 3)\n");
    firmware.setSelectedId(3);
    std::printf("the tick that still sees 224 v4.3 running\n");
    view.refresh();
    check(commands.sets.size() == 1 && commands.sets[0] == 2, "one command, for index 2");
    check(firmware.getSelectedId() == 3, "the choice is not undone");

    std::printf("further ticks while the job boots\n");
    view.refresh();
    check(commands.sets.size() == 1, "still one command");
    check(firmware.getSelectedId() == 3, "the choice is kept");

    std::printf("the processor reports the set its job prepares\n");
    snapshot.pendingRomSet = 2;
    view.refresh();
    check(commands.sets.size() == 1, "still one command");
    check(firmware.getSelectedId() == 3, "the box shows it");

    std::printf("its machine goes live\n");
    snapshot.pendingRomSet = -1;
    snapshot.selectedRomSet = 2;
    view.refresh();
    check(commands.sets.size() == 1, "still one command");
    check(firmware.getSelectedId() == 3, "the box shows it");

    std::printf("150 ms of the message loop: the editor's own ticks, and any stale notification\n");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    check(commands.sets.size() == 1, "still one command");
    check(firmware.getSelectedId() == 3, "still 224 v4.4");

    std::printf("a pick whose boot job ends without a machine\n");
    firmware.setSelectedId(1);   // 224 v3.2, index 0
    view.refresh();
    check(commands.sets.size() == 2 && commands.sets[1] == 0, "one command, for index 0");
    snapshot.pendingRomSet = 0;   // the job is booting it
    view.refresh();
    check(firmware.getSelectedId() == 1, "the box shows it");
    snapshot.pendingRomSet = -1;   // ... and stopped: 224 v4.4 still runs
    view.refresh();
    check(commands.sets.size() == 2, "no command for the failure");
    check(firmware.getSelectedId() == 3, "the box is back on the running set");

    std::printf("a set the firmware lists as unsupported\n");
    snapshot.romSets[0].unsupported = true;   // 224 v2.2
    view.refresh();                           // the list is rebuilt, the box follows it
    check(firmware.getSelectedId() == 3, "the box still shows the running set");
    firmware.setSelectedId(1);
    view.refresh();
    check(commands.sets.size() == 2, "no command for a set that cannot be loaded");
    check(firmware.getSelectedId() == 3, "the box is back on the running set");

    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    check(commands.sets.size() == 2, "no command from a notification");
    check(commands.others == 0, "no other command");

    std::printf("%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
