#include <gtest/gtest.h>
#include <filesystem>
#include <hw_logo.h>

TEST(HwLogo, IntelCoreGeneration) {
    EXPECT_EQ(hw_logo::cpu("Intel(R) Core(TM) i7-920 CPU").asset, "cpu/intel-core-i7-gen1-blue.png");
    EXPECT_EQ(hw_logo::cpu("Intel(R) Core(TM) i5-2500K").asset, "cpu/intel-core-i5-gen2.png");
    EXPECT_EQ(hw_logo::cpu("Intel(R) Core(TM) i7-8700K CPU @ 3.70GHz").asset, "cpu/intel-core-i7-gen6.png");
    EXPECT_EQ(hw_logo::cpu("Intel(R) Core(TM) i7-1165G7 @ 2.80GHz").asset, "cpu/intel-core-i7-gen11.png");
    EXPECT_EQ(hw_logo::cpu("13th Gen Intel(R) Core(TM) i9-13900K").asset, "cpu/intel-core-i9-gen11.png");
    EXPECT_EQ(hw_logo::cpu("Intel(R) Core(TM) i9-10900K").asset, "cpu/intel-core-i9-gen10.png");
}

TEST(HwLogo, IntelOther) {
    EXPECT_EQ(hw_logo::cpu("Intel(R) Core(TM) Ultra 7 155H").asset, "cpu/intel-core-ultra-7.png");
    EXPECT_EQ(hw_logo::cpu("Intel(R) Core(TM) Ultra 7 365").asset, "cpu/intel-core-ultra-7-series3.png");
    EXPECT_EQ(hw_logo::cpu("Intel(R) Core(TM) 5 120U").asset, "cpu/intel-core-5.png");
    EXPECT_EQ(hw_logo::cpu("Intel(R) Xeon(R) Gold 6130").asset, "cpu/intel-xeon-gold.png");
    EXPECT_EQ(hw_logo::cpu("Intel(R) Core(TM)2 Quad CPU Q6600").asset, "cpu/intel-core2-quad.png");
}

TEST(HwLogo, Amd) {
    EXPECT_EQ(hw_logo::cpu("AMD Ryzen 7 5800X 8-Core Processor").asset, "cpu/amd-ryzen-7.png");
    EXPECT_EQ(hw_logo::cpu("AMD Ryzen 9 9950X 16-Core Processor").asset, "cpu/amd-ryzen-9-new.png");
    EXPECT_EQ(hw_logo::cpu("AMD FX(tm)-8350 Eight-Core Processor").asset, "cpu/amd-fx.png");
    EXPECT_EQ(hw_logo::cpu("AMD Athlon(tm) II X2 250").asset, "cpu/amd-athlon-ii-x2.png");
}

TEST(HwLogo, Os) {
    EXPECT_EQ(hw_logo::os("Fedora Linux 44 (Workstation Edition)").asset, "logo/fedora.png");
    EXPECT_EQ(hw_logo::os("Windows 11 Pro").caption, "Windows 11");
    EXPECT_TRUE(hw_logo::os("Plan 9").asset.empty());
}

TEST(HwLogo, GpuCaption) {
    EXPECT_EQ(hw_logo::gpu("NVIDIA GeForce RTX 4090").caption, "RTX 4090");
    EXPECT_EQ(hw_logo::gpu("AMD Radeon RX 7900 XTX").caption, "RX 7900 XTX");
}

TEST(HwLogo, Unknown) {
    EXPECT_EQ(hw_logo::cpu("Apple M1").asset, "cpu/apple-m1.png");
    EXPECT_EQ(hw_logo::cpu("Some Unknown CPU").asset, "cpu/generic.png");
    EXPECT_TRUE(hw_logo::gpu("Virtio GPU").asset.empty());
}

TEST(HwLogo, Gpu) {
    EXPECT_EQ(hw_logo::gpu("NVIDIA GeForce RTX 4090").asset, "gpu/nvidia-geforce-rtx.png");
    EXPECT_EQ(hw_logo::gpu("Intel(R) Arc(TM) A770").asset, "gpu/intel-arc-graphics.png");
    EXPECT_EQ(hw_logo::gpu("AMD Radeon RX 7900 XTX").asset, "gpu/amd-radeon-graphics.png");
}

// every logo we point to must exist in assets/img
TEST(HwLogo, AllAssetsExist) {
    for (const char* cpu : { "Intel(R) Core(TM) i3-3220", "Intel(R) Core(TM) i9-7900X", "AMD Ryzen 5 7600", "AMD Ryzen AI 9 HX 370", "VIA Eden X4" }) {
        const auto asset = hw_logo::cpu(cpu).asset;
        EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "img" / asset)) << asset;
    }
}
