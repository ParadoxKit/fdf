
#if defined(FDF_USE_CPP_MODULES)
    import std;
    import fdf;
#else
    #include "fdf.h"
    #include <fstream>
    #include <iostream>
#endif




namespace
{
    using namespace fdf;
    using namespace fdf::detail;

    constexpr std::string_view TOKEN_TYPE_TO_STRING[] =
    {
        "NonExisting     ",
        "Invalid         ",
        "NewLine         ",
        "EndOfFile       ",
        "Comment         ",

        "At              ",
        "Equal           ",
        "Comma           ",
        "Colon           ",

        "CurlyBraceOpen  ",
        "CurlyBraceClose ",
        "SquareBraceOpen ",
        "SquareBraceClose",

        "Identifier      ",
        "Keyword         ",
        "IntLiteral      ",
        "FloatLiteral    ",

        "StringLiteral   ",
        "HexLiteral      ",
        "VersionLiteral  ",
        "TimestampLiteral",

        "EvaluateLiteral "
    };

    constexpr std::string_view ENTRY_TYPE_TO_STRING[] =
    {
        "Invalid  ",
        "Null     ",
        
        "Bool     ",
        "Int      ",
        "UInt     ",
        "Float    ",
        
        "String   ",
        "Hex      ",
        "Version  ",
        "Timestamp",
        
        "Array    ",
        "Map      "
    };

    void PrintAllTokens(std::string_view inFile, std::string_view outFile)
    {
        std::ifstream file(inFile.data());
        if(!file)
            return;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        Tokenizer tokenizer = Tokenizer(content);
    
        std::vector<Token> tokens;
        std::vector<std::string_view> views;
        tokens.push_back(tokenizer.Current());
        views.push_back(tokens[0].ToView(content));
        while(true)
        {
            Token token = tokenizer.PeekAndAdvance();
            tokens.push_back(token);
            views.push_back(token.ToView(content));
    
            if(token.type == TokenType::EndOfFile || token.type == TokenType::Invalid)
                break;
        }
    
        std::string buffer;
        auto addToBuffer = [&buffer](std::string_view value)
        {
            for(char c : value)
            {
                if(c == '\n')
                    buffer.append("\\n");
                else
                    buffer.push_back(c);
            }
        };
    
        size_t tokenIndex = 0;
        while(tokenIndex < tokens.size())
        {
            addToBuffer(std::format("id={:03}--Type={}--Extra8={:02}--Value={}", tokenIndex, TOKEN_TYPE_TO_STRING[static_cast<size_t>(tokens[tokenIndex].type)], tokens[tokenIndex].extra8, views[tokenIndex]));
            buffer.push_back('\n');
            tokenIndex++;
        }
    
        std::ofstream outfile(outFile.data());
        if(outfile.is_open())
            outfile << buffer;
    }

    void PrintAllEntries(const std::vector<Entry>& entries, std::string_view outFile)
    {
        std::string buffer;
        auto addToBuffer = [&buffer](std::string_view value)
        {
            for(char c : value)
            {
                if(c == '\n')
                    buffer.append("\\n");
                else
                    buffer.push_back(c);
            }
        };

        std::string temp;
        for(const Entry& entry : entries)
        {
            for(uint8_t i = 0; i < entry.depth; i++)
                addToBuffer("    ");

            addToBuffer(std::format("Type={}--Size={:03}--Name={:<20}--Value={:<50}--Comment={}", ENTRY_TYPE_TO_STRING[static_cast<size_t>(entry.type)], entry.size, entry.fullIdentifier, entry.DataToView(temp), entry.comment));
            buffer.push_back('\n');
        }

        std::ofstream outfile(outFile.data());
        if(outfile.is_open())
            outfile << buffer;
    }

    struct TestDirectories
    {
        TestDirectories(const std::filesystem::path& file)
        {
            inputFile = file.generic_string();
            outputFile = FDF_TEST_DIRECTORY "/output/" + file.stem().generic_string();
            
            tokenizedFile = outputFile + "-Tokenized.txt";
            entriesFile   = outputFile + "-Entries.txt";
            userTypesFile = outputFile + "-UserTypes.txt";
            outputFile    = outputFile + "-Output.txt";
        }

        std::string inputFile, outputFile, tokenizedFile, entriesFile, userTypesFile;
    };

    void ResolveDirectories(std::string& inputFile, std::string& tokenizedFile, std::string& entriesFile, std::string& userTypesFile, std::string_view inputFileNameWithoutExtension, bool bInsideTestDir)
    {
        if(bInsideTestDir)
            inputFile  = std::format("{}/{}.txt", FDF_TEST_DIRECTORY, inputFileNameWithoutExtension);
        else
            inputFile = std::format("{}/designs/{}.txt", FDF_ROOT_DIRECTORY, inputFileNameWithoutExtension);

        tokenizedFile = std::format("{}/output/{}-Tokenized.txt", FDF_TEST_DIRECTORY, inputFileNameWithoutExtension);
        entriesFile   = std::format("{}/output/{}-Entries.txt",   FDF_TEST_DIRECTORY, inputFileNameWithoutExtension);
        userTypesFile = std::format("{}/output/{}-UserTypes.txt", FDF_TEST_DIRECTORY, inputFileNameWithoutExtension);
    }

    std::vector<TestDirectories> filesToTest;
    size_t longestFilename = 0;
}




namespace fdf::detail
{
    using namespace std::chrono_literals;

    bool TestFiles()
    {
        bool bResult = true;
        for(size_t i = 0; i < filesToTest.size(); i++)
        {
            const TestDirectories& directories = filesToTest[i];
            std::cout << std::format("[{:02}/{:02}] {:<{}}", i + 1, filesToTest.size(), directories.inputFile, longestFilename);

            auto startTime = std::chrono::high_resolution_clock::now();
            Reader reader = Reader(std::filesystem::path(directories.inputFile));
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = duration_cast<std::chrono::microseconds>(endTime - startTime);

            std::string durationString = std::format("{:.5f}ms", duration.count() / 1'000.0);
            std::cout << std::format(" -- Result: {:<7} -- Took: {:<9}\n", reader.IsValid()? "SUCCESS" : "FAIL", durationString);
            
            PrintAllTokens(directories.inputFile, directories.tokenizedFile);
            PrintAllEntries(reader.entries, directories.entriesFile);
            PrintAllEntries(reader.userTypes, directories.userTypesFile);

            bResult = bResult && reader.IsValid();
        }

        return bResult;
    }
}




int main()
{
    std::filesystem::path currentDesignFile = FDF_ROOT_DIRECTORY "/designs/Design_4.txt";
    std::filesystem::path testDir = FDF_TEST_DIRECTORY;
    std::filesystem::path outputDir = FDF_TEST_DIRECTORY "/output";
    bool result = true;

    if(!std::filesystem::exists(outputDir))
        std::filesystem::create_directory(outputDir);

    if(std::filesystem::exists(currentDesignFile))
        filesToTest.emplace_back(std::move(currentDesignFile));
    
    for(const auto& entry : std::filesystem::directory_iterator(testDir))
    {
        if(entry.is_regular_file() && entry.path().stem() != "CMakeLists" && (entry.path().extension() == ".txt" || entry.path().extension() == ".fdf"))
        {
            size_t length = filesToTest.emplace_back(entry.path()).inputFile.size();
            if(length > longestFilename)
                longestFilename = length;
        }
    }

    std::cout << std::format("Testing fdf files -- Found {} files\n\n", filesToTest.size());
    return TestFiles()? 0 : -1;
}
