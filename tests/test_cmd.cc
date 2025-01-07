#include "application.h"
#include "cmd.h"
#include <gtest/gtest.h>

using namespace meha;

#define TEST_CASE CmdArgsTest

TEST(TEST_CASE, AddFlag)
{
    ArgParser parser;
    EXPECT_TRUE(parser.addArg(Flag("--help", "-h", "Print help message", false)));
    Flag flag;
    flag.setKey("--verbose", "-v").setRequired(true).setHelp("Print verbose output");
    EXPECT_TRUE(parser.addFlag(flag));
    EXPECT_EQ(parser.dumpAll(), "Flag(--verbose, -v: Print verbose output)\nFlag(--help, -h: Print help message)\n");
}

TEST(TEST_CASE, AddOption)
{
    ArgParser parser;
    EXPECT_TRUE(parser.addOption(Option("--output", "-o", "Output file", false, "stdout")));
    Option option;
    option.setKey("--inpput", "-i").setRequired(true).setHelp("Input file").setDefaultValue("stdin");
    EXPECT_TRUE(parser.addOption(option));
    EXPECT_EQ(parser.dumpAll(), "Option(--inpput, -i: Input file, value: stdin)\nOption(--output, -o: Output file, value: stdout)\n");
}

TEST(TEST_CASE, ResetArgPattern)
{
    ArgParser parser;
    EXPECT_TRUE(parser.addFlag(Flag("--verbose", "-v", "Print verbose output", true)));
    EXPECT_TRUE(parser.addOption(Option("--output", "-o", "Output file", false, "stdout")));
    EXPECT_EQ(parser.dumpAll(), "Flag(--verbose, -v: Print verbose output)\nOption(--output, -o: Output file, value: stdout)\n");
    EXPECT_FALSE(parser.isParsed());
    parser.reset();
    EXPECT_FALSE(parser.isParsed());
}
TEST(TEST_CASE, ParseArgsSuccess)
{
    ArgParser parser;

    EXPECT_TRUE(parser.addArg(Flag("--help", "-h", "Print help message", false)));
    EXPECT_TRUE(parser.addFlag(Flag("--verbose", "-v", "Verbose mode", true)));

    EXPECT_TRUE(parser.addArg(Option("--output", "-o", "Output file", false, "stdout")));
    EXPECT_TRUE(parser.addOption(Option("--input", "-i", "Input file", true, "stdin")));

    const char *argv1[] = {"program", "--help", "-v", "--output=output.txt"};
    EXPECT_TRUE(parser.parseArgs(4, const_cast<char **>(argv1))); // NOTE 注意这里必须使用char**而不能用char*[]，因为const_cast只能去掉指针或引用的const属性
    EXPECT_TRUE(parser.isFlagSet("--help"));
    EXPECT_TRUE(parser.isFlagSet("-h"));
    EXPECT_TRUE(parser.isFlagSet("--verbose"));
    EXPECT_TRUE(parser.isFlagSet("-v"));
    EXPECT_FALSE(parser.isFlagSet("--nonexisit"));
    EXPECT_FALSE(parser.isFlagSet("-n"));
    // FIXME 这里由于doParse里面保存的是std::string,所以命令行设置了的--output就是std::string,而没有设置的-i就是const char*
    // 最好的解决方案就是整个命令行参数都保存为std::string，这样也符合使用逻辑
    EXPECT_EQ(std::any_cast<std::string>(parser.getOptionValue("--output").value()), "output.txt");
    EXPECT_EQ(std::any_cast<const char *>(parser.getOptionValue("-i").value()), "stdin");
}

TEST(TEST_CASE, ParseArgsFailure)
{
    ArgParser parser;

    EXPECT_TRUE(parser.addArg(Flag("--help", "-h", "Print help message", false)));
    EXPECT_TRUE(parser.addFlag(Flag("--verbose", "-v", "Verbose mode", true)));

    EXPECT_TRUE(parser.addArg(Option("--output", "-o", "Output file", false, "stdout")));
    EXPECT_TRUE(parser.addOption(Option("--input", "-i", "Input file", true, "stdin")));

    const char *argv2[] = {"program", "--help"};
    EXPECT_FALSE(parser.parseArgs(2, const_cast<char **>(argv2)));
}

int main(int argc, char *argv[])
{
    Application app;
    return app.boot(BootArgs{
        .argc = argc,
        .argv = argv,
        .configFile = "/home/will/Workspace/Devs/projects/server-framework/misc/config.yml",
        .mainFunc = [](int argc, char **argv) -> int {
            ::testing::InitGoogleTest(&argc, argv);
            return RUN_ALL_TESTS();
        }});
}