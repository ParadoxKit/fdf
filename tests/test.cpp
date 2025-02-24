
#if defined(FDF_USE_CPP_MODULES)
    import std;
    import fdf;
#else
    #include "fdf.h"
    #include <iostream>
#endif




namespace fdf::detail
{
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

    struct TestDirectories
    {
        TestDirectories(const std::filesystem::path& file)
        {
            inputFile = file.generic_string();
            outputFile = FDF_TEST_DIRECTORY "/output/" + file.stem().generic_string();

            tokenizedFile = outputFile + "-Tokenized.txt";
            entriesFile = outputFile + "-Entries.txt";
            outputFile = outputFile + "-Output.txt";
        }

        std::string inputFile, outputFile, tokenizedFile, entriesFile;
    };



    std::vector<TestDirectories> filesToTest;
    size_t longestFilename = 0;
    std::string output;




    struct Test
    {
        static void PrintAllTokens(std::string_view inFile, std::string_view outFile)
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
                Token token = tokenizer.Advance();
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

        static void PrintAllEntries(const std::vector<Entry>& entries, std::string_view outFile)
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
                addToBuffer(std::format("{:<{}}Type={}--Size={:03}--Name={:<20}--Value={:<50}--Comment={}", "", 4 * entry.depth, ENTRY_TYPE_TO_STRING[static_cast<size_t>(entry.type)], entry.size, entry.fullIdentifier, entry.DataToView(temp), entry.comment));
                buffer.push_back('\n');
            }

            std::ofstream outfile(outFile.data());
            if(outfile.is_open())
                outfile << buffer;
        }




        static bool ErrorCallback(Error error, std::string_view message)
        {
            if(output.empty())
                output = std::format("{}: {}"    ,         IsWarning(error)? "Warning" : "Error", message);
            else
                output = std::format("{}\n{}: {}", output, IsWarning(error)? "Warning" : "Error", message);

            return true;
        }

        


        static void PrintLastSuccessfullyParsedEntry(auto& io)
        {
            size_t lastID = -1;
            for(size_t i = io.entries.size() - 1; i != -1; i++)
            {
                if(io.entries[i].type != Type::Invalid)
                {
                    lastID = i;
                    break;
                }
            }

            std::cout << std::format("-----LAST SUCCESSFULLY PARSED ENTRY: {} (id: {})-----\n", lastID == -1? "<None>" : io.entries[lastID].fullIdentifier, static_cast<int64_t>(lastID));
        }

        static bool ParseTest()
        {
            bool bResult = true;
            for(size_t i = 0; i < filesToTest.size(); i++)
            {
                const TestDirectories& directories = filesToTest[i];
                std::cout << std::format("[{:02}/{:02}] {:<{}}", i + 1, filesToTest.size(), directories.inputFile, longestFilename);

                auto startTime = std::chrono::high_resolution_clock::now();
                IO<ErrorCallback> io;
                bool bSuccess = io.Parse(std::filesystem::path(directories.inputFile));
                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = duration_cast<std::chrono::nanoseconds>(endTime - startTime);

                std::string durationString = std::format("{:.6f}ms", duration.count() / 1'000'000.0);
                std::cout << std::format(" -- Result: {:<7} -- Took: {:<9}\n", bSuccess? "SUCCESS" : "FAIL", durationString);

                if(!bSuccess)
                    PrintLastSuccessfullyParsedEntry(io);

                if(!output.empty())
                {
                    std::cout << output << "\n\n";
                    output.clear();
                }
                else
                    std::cout << '\n';
                
                PrintAllTokens(directories.inputFile, directories.tokenizedFile);
                PrintAllEntries(io.entries, directories.entriesFile);

                bResult = bResult && bSuccess;
            }

            return bResult;
        }




        // We intentionally print each one via a "Entry::GetValue" instead of "Entry::DataToView" so we can test more of the code
        // TODO: Maybe automate ReadTest so we don't need to implement each Entry by hand? (and manually adjust formatting (currently 24))
        static bool ReadTest()
        {
            IO io;
            if(!io.Parse(std::filesystem::path(filesToTest[0].inputFile)))
            {
                std::cout << "Failed to parse the design file... Should never happen unless initial parse failed too!\n";
                PrintLastSuccessfullyParsedEntry(io);
                return false;
            }


            bool bResult = true;


            {
                const Entry& entry = io.GetEntry("appVersion");
                std::cout << std::format("{:<24}  ->  ", "appVersion");
                if(entry.type == Type::Version)
                {
                    auto val = entry.GetValue<uint64_t>();
                    std::cout << val[0] << '.' << val[1] << '.' << val[2];
                    if(val.size() == 4)
                        std::cout << '.' << val[3];

                    std::cout << '\n';
                }
                else
                {
                    bResult = false;
                    std::cout << "<ERROR>\n";
                }
            }


            {
                const Entry& entry = io.GetEntry("uuid");
                std::cout << std::format("{:<24}  ->  ", "uuid");
                if(entry.type == Type::String)
                {
                    auto val = entry.GetValue<char>();
                    for(char c : val)
                    {
                        if(c == '\n')
                            std::cout << "\\n";
                        else
                            std::cout << c;
                    }
                    std::cout << '\n';
                }
                else
                {
                    bResult = false;
                    std::cout << "<ERROR>\n";
                }
            }


            {
                const Entry& entry = io.GetEntry("gameSettings1.resolution");
                std::cout << std::format("{:<24}  ->  ", "gameSettings1.resolution");
                if(entry.type == Type::Int && entry.size == 2)
                {
                    auto val = entry.GetValue<int64_t>();
                    std::cout << val[0] << 'x' << val[1] << '\n';
                }
                else
                {
                    bResult = false;
                    std::cout << "<ERROR>\n";
                }
            }


            {
                const Entry& entry = io.GetEntry("NON_EXISTING");
                std::cout << std::format("{:<24}  ->  ", "NON_EXISTING");
                if(entry.type == Type::Invalid)
                {
                    std::cout << "<NON_EXISTING>\n";
                }
                else
                {
                    bResult = false;
                    std::cout << "<ERROR>\n";
                }
            }


            return bResult;
        }




        static bool WriteTest()
        {
            std::cout << "<Placeholder>\n";
            return true;
        }
    };
}




int main()
{
    using namespace fdf::detail;

    std::filesystem::path currentDesignFile = FDF_ROOT_DIRECTORY "/designs/Design_5.txt";
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


    constexpr const char* separator = "--------------------------------------------------\n";
    bool bResult = true;

    std::cout << std::format("Parse test -- Found {} files\n{}", filesToTest.size(), separator);
    bResult = bResult && Test::ParseTest();
    std::cout << std::format("\n{}{}\nRead test -- file: {}\n{}", separator, separator, filesToTest[0].inputFile, separator);
    bResult = bResult && Test::ReadTest();
    std::cout << std::format("\n{}{}\Write test -- file: {}\n{}", separator, separator, "<Placeholder>", separator);
    bResult = bResult && Test::WriteTest();

    return bResult? 0 : -1;
}
