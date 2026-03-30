#include "internal/osc_builder.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void TestUsbPathsUseZeroPaddedIndices() {
    Expect(WingConnector::OscBuilder::UsbPath(9, "out", "name") == "/io/out/USB/09/name",
           "USB OSC paths should use zero-padded slot indices");
    Expect(WingConnector::OscBuilder::UsbPath(10, "in", "mode") == "/io/in/USB/10/mode",
           "double-digit USB paths should remain unchanged");
}

void TestCardPathsUseZeroPaddedIndices() {
    Expect(WingConnector::OscBuilder::CardPath(7, "name") == "/io/out/CRD/07/name",
           "CARD OSC paths should use zero-padded slot indices");
}

}  // namespace

int main() {
    TestUsbPathsUseZeroPaddedIndices();
    TestCardPathsUseZeroPaddedIndices();

    std::cout << "osc_builder_tests: OK\n";
    return 0;
}
