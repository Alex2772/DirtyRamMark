#include <AUI/Common/AByteBuffer.h>
#include <memory_info.h>
#include <privileges.h>
#include <AUI/IO/AFileInputStream.h>
#include <AUI/IO/AFileOutputStream.h>
#include <AUI/Platform/AProcess.h>

static AString dmiencode() {
    static AString output = [] {
        try {
            return AString::fromUtf8(AByteBuffer::fromStream(AFileInputStream("/tmp/dmidecode_output.txt")));
        } catch (...) {

        }
        if (!privileges::isGranted()) {
            throw privileges::Required("dmidecode needs root to read the memory modules info");
        }
        auto dmidecode = AProcess::create({.executable = "/usr/bin/pkexec", .args = AProcess::ArgSingleString{"dmidecode"}});
        AByteBuffer output;
        AObject::connect(dmidecode->stdOut, AObject::GENERIC_OBSERVER, [&](AByteBuffer o) {
            output << o;
        });
        dmidecode->run();
        dmidecode->waitForExitCode();
        AFileOutputStream("/tmp/dmidecode_output.txt") << output;
        return AString::fromUtf8(output);
    }();
    return output;
}

AJson::Object memoryInfo() {
    auto result = parseDmidecodeOutput(dmiencode());
    return result;
}
