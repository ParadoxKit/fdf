
#if !defined(FDF_USE_CPP_MODULES) || defined(FDF_CPP_FILE)
    #include <cstdint>
    #include <type_traits>
    #include <cassert>
    #include <string>
    #include <vector>
    #include <format>
    #include <filesystem>
    #include <fstream>
    #include <cctype>

    #define FDF_EXPORT
#endif




// TODO: Maybe add an option to lazily evaluate? (store every value as a string and don't process anything until requested)
// TODO: Add explicitly specified type information to entries
// TODO: Add some kind of enum type (to file format)




FDF_EXPORT namespace fdf
{
    using namespace std::literals;

    struct Date
    {
        constexpr Date() noexcept
            : year(0), month(0), day(0)  { }
        constexpr Date(uint16_t _year, uint8_t _month = 0u, uint8_t _day = 0u) noexcept
            : year(_year), month(_month), day(_day)  { }
    
    public:
        uint16_t year;
        uint8_t month;
        uint8_t day;
    };
    
    struct Time
    {
        constexpr Time() noexcept
            : hour(0), minute(0), second(0)  { }
        constexpr Time(uint8_t _hour, uint8_t _minute = 0u, uint8_t _second = 0u) noexcept
            : hour(_hour), minute(_minute), second(_second)  { }
    
    public:
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
    };
    
    struct PrecisionTime
    {
        constexpr PrecisionTime() noexcept
            : millisecond(0), microsecond(0), nanosecond(0)  { }
        constexpr PrecisionTime(uint16_t _millisecond, uint16_t _microsecond = 0u, uint16_t _nanosecond = 0u) noexcept
            : millisecond(_millisecond), microsecond(_microsecond), nanosecond(_nanosecond)  { }
    
    public:
        uint16_t millisecond;
        uint16_t microsecond;
        uint16_t nanosecond;
    };
    
    struct DateTime
    {
        constexpr DateTime() noexcept = default;
        constexpr DateTime(Date _date, Time _time = {}) noexcept
            : date(_date), time(_time)  { }
        constexpr DateTime(uint16_t _year, uint8_t _month = 0u, uint8_t _day = 0u, uint8_t _hour = 0u, uint8_t _minute = 0u, uint8_t _second = 0u) noexcept
            : date(_year, _month, _day), time(_hour, _minute, _second)  { }
    
    public:
        Date date;
        Time time;
    };
    
    struct Timestamp
    {
        constexpr Timestamp() noexcept = default;
        constexpr Timestamp(Date _date, Time _time = {}, PrecisionTime _precisionTime = {}) noexcept
            : date(_date), time(_time), precisionTime(_precisionTime)  { }
        constexpr Timestamp(uint16_t _year, uint8_t _month = 0u, uint8_t _day = 0u, uint8_t _hour = 0u, uint8_t _minute = 0u, uint8_t _second = 0u, uint16_t _millisecond = 0u, uint16_t _microsecond = 0u, uint16_t _nanosecond = 0u) noexcept
            : date(_year, _month, _day), time(_hour, _minute, _second), precisionTime(_millisecond, _microsecond, _nanosecond)  { }
    
    public:
        Date date;
        Time time;
        PrecisionTime precisionTime;
    };

    static_assert(std::is_trivially_destructible_v<Timestamp>, "That's not right...");


    enum class Type : uint8_t
    {
        Invalid,
        Null,

        Bool,
        Int,
        UInt,
        Float,

        String,
        Hex,
        Version,
        Timestamp,

        Array,
        Map
    };


    struct Style
    {
        consteval Style() noexcept = default;

    public:
        uint8_t singleLineCommentLimit = 80;
        uint8_t tabSize = 4;
        bool bUseSpacesOverTabs = true;
        bool bParanthesisOnNewLine = true;
        bool bCommasOnArrays = true;
        bool bCommasOnObjects = true;
        bool bCommasOnLastElement = true;
        bool bSingleLineForShortArrays = true;
        bool bSingleLineForShortObjects = true;
        bool bSpaceWithinParanthesis = true;
        bool bSpaceBeforeAndAfterEqualSign = false;
        bool bGroupSimilarTypes = true;
        bool bUppercaseHex = true;
        bool bEmptyLineAtTheEndOfTheFile = true;
        bool bAlignCloseComments = true;
        bool bUseEqualSignForSingleLineArraysAndObjects = false;
    };
}




















namespace fdf::detail
{
    constexpr std::string_view EVALUATE_LITERAL_TEXT = "Evaluate Literal";
    constexpr std::string_view NONE_TEXT  = "<NONE>";
    constexpr std::string_view NULL_TEXT  = "<NULL>";
    constexpr std::string_view TRUE_TEXT  = "<TRUE>";
    constexpr std::string_view FALSE_TEXT = "<FALSE>";
    constexpr std::string_view ARRAY_TEXT = "<ARRAY>";
    constexpr std::string_view MAP_TEXT   = "<MAP>";

    constexpr uint64_t  INT64_MAX_VALUE = std::numeric_limits< int64_t>::max();
    constexpr uint64_t UINT64_MAX_VALUE = std::numeric_limits<uint64_t>::max();
    constexpr double   DOUBLE_MAX_VALUE = std::numeric_limits<  double>::max();

    constexpr size_t VARIANT_SIZE = 5 * sizeof(int64_t);  // 5x 64 bit value
    constexpr size_t VARIANT_32BIT_ELEMENT_COUNT = VARIANT_SIZE / sizeof(int32_t);
    constexpr size_t VARIANT_64BIT_ELEMENT_COUNT = VARIANT_SIZE / sizeof(int64_t);
    constexpr size_t VARIANT_DYNAMIC_STRING_HARD_LIMIT = (VARIANT_SIZE * 2.5);
    union Variant
    {
        bool     b[VARIANT_SIZE];
        int64_t  i[VARIANT_64BIT_ELEMENT_COUNT];
        uint64_t u[VARIANT_64BIT_ELEMENT_COUNT];
        double   f[VARIANT_64BIT_ELEMENT_COUNT];
        char   str[VARIANT_SIZE];

        // For now, we store it as string
        //Timestamp timestamp[VARIANT_SIZE / sizeof(Timestamp)];

        struct String
        {
            char& operator[](size_t i)       noexcept  { return data[i]; }
            char  operator[](size_t i) const noexcept  { return data[i]; }

            void Delete() noexcept
            {
                delete[] data;
                capacity = 0;
            }
            void Release() noexcept
            {
                data = nullptr;
                capacity = 0;
            }
            void Reserve(size_t size) noexcept
            {
                char* newData = new char[size];
                if(data != nullptr)
                {
                    memcpy(newData, data, capacity);
                    delete[] data;
                }
                data = newData;
                capacity = size;
                RefreshView();
            }
            void InitialAllocate(size_t size) noexcept
            {
                data = new char[size];
                capacity = size;
                RefreshView();
            }
            String Copy() const noexcept
            {
                String other{};
                if(capacity > 0 && data != nullptr)
                {
                    other.InitialAllocate(capacity);
                    memcpy(other.data, data, capacity);
                    other.RefreshView();
                }

                return other;
            }
            String Move() noexcept
            {
                String other = *this;
                *this = {};
                return other;
            }
            void RefreshView() noexcept
            {
                view = std::string_view(data, capacity);
            }

            size_t capacity;
            char* data;
            std::string_view view;
        } strDynamic;

    public:
        constexpr  Variant() noexcept  { }
        constexpr ~Variant() noexcept  { }
    };




    struct Entry
    {
        Type type = Type::Invalid;
        Type subType = Type::Invalid;  // Subtype for Array and Map
        uint8_t depth = 0; // depth of the entry (0 for top level, 1 for child of top level, 2 for grandchild of top level, ...)
        uint8_t identifierSize = 0;
        // uint16_t fullIdentifierSize = 0;  // From old implementation where we manage our own memory
        uint32_t size = 0; // If Array or Map this is count of top level childs, otherwise type specific (for example: character count for string)
        //void* data = nullptr;              // From old implementation where we manage our own memory

        std::string fullIdentifier;
        std::string comment;
        Variant data;

    public:
        constexpr Entry() noexcept = default;
        constexpr ~Entry() noexcept
        {
            if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > VARIANT_SIZE - 1)
                data.strDynamic.Delete();
        }



        constexpr Entry(const Entry& other) noexcept
            : type(other.type), subType(other.subType), depth(other.depth), identifierSize(other.identifierSize), size(other.size), fullIdentifier(other.fullIdentifier), comment(other.comment)
        {
            if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > VARIANT_SIZE - 1)
                data.strDynamic = other.data.strDynamic.Copy();
            else
                data = other.data;
        }

        constexpr Entry(Entry&& other) noexcept
            : type(other.type), subType(other.subType), depth(other.depth), identifierSize(other.identifierSize), size(other.size), fullIdentifier(std::move(other.fullIdentifier)), comment(std::move(other.comment))
        {
            if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > VARIANT_SIZE - 1)
                data.strDynamic = other.data.strDynamic.Move();
            else
                data = other.data;

            other.type = Type::Invalid;
        }



        constexpr Entry& operator=(const Entry& other) noexcept
        {
            if(this != &other)
            {
                type = other.type;
                subType = other.subType;
                depth = other.depth;
                identifierSize = other.identifierSize;
                size = other.size;
                fullIdentifier = other.fullIdentifier;
                comment = other.comment;

                if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > VARIANT_SIZE - 1)
                    data.strDynamic.Delete();

                if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > VARIANT_SIZE - 1)
                    data.strDynamic = other.data.strDynamic.Copy();
                else
                    data = other.data;
            }
            return *this;
        }

        constexpr Entry& operator=(Entry&& other) noexcept
        {
            if(this != &other)
            {
                type = other.type;
                subType = other.subType;
                depth = other.depth;
                identifierSize = other.identifierSize;
                size = other.size;
                fullIdentifier = std::move(other.fullIdentifier);
                comment = std::move(other.comment);

                if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > VARIANT_SIZE - 1)
                    data.strDynamic.Delete();
                
                if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > VARIANT_SIZE - 1)
                    data.strDynamic = other.data.strDynamic.Move();
                else
                    data = other.data;

                other.type = Type::Invalid;
            }
            return *this;
        }




        constexpr std::string_view GetIdentifier() const noexcept
        {
            return std::string_view(fullIdentifier.data() + fullIdentifier.size() - identifierSize, identifierSize);
        }
        constexpr std::string_view GetIdentifierWithDot() const noexcept
        {
            return std::string_view(fullIdentifier.data() + fullIdentifier.size() - identifierSize - 1, identifierSize + 1);
        }
        constexpr std::string_view DataToView(std::string& temp) const noexcept
        {
            if(type == Type::Invalid)
                return NONE_TEXT;
            if(type == Type::Null)
                return NULL_TEXT;
            if(type == Type::Array)
                return ARRAY_TEXT;
            if(type == Type::Map)
                return MAP_TEXT;
            if(type == Type::Bool)
                return data.b[0]? TRUE_TEXT : FALSE_TEXT;

            if(type == Type::String || type == Type::Hex || type == Type::Timestamp)
            {
                if(size > VARIANT_SIZE - 1)
                    return data.strDynamic.view.substr(0, size); 
                return std::string_view(data.str, size);
            }

            if(type == Type::Version)
            {
                temp = std::format("{}.{}.{}.{}", data.u[0], data.u[1], data.u[2], data.u[3]);
                return temp;
            }


            if(type == Type::Int)
            {
                temp = std::format("{}", data.i[0]);
                for(int i = 1; i < size; i++)
                    temp = std::format("{}x{}", temp, data.i[i]);

                return temp;
            }

            if(type == Type::UInt)
            {
                temp = std::format("{}", data.u[0]);
                for(int i = 1; i < size; i++)
                    temp = std::format("{}x{}", temp, data.u[i]);

                return temp;
            }

            if(type == Type::Float)
            {
                temp = std::format("{}", data.f[0]);
                for(int i = 1; i < size; i++)
                    temp = std::format("{}x{}", temp, data.f[i]);

                return temp;
            }

            assert(false);
            return {};
        };
    };

    struct EntryWIP
    {
        Type type;
        bool bContainsCurlyScopes;
        bool bContainsSquareScopes;
        bool bContainsString;
        uint32_t scopeCount;
        size_t startIndex;
        size_t endIndex;
        std::string_view identifier;
        std::string_view value;
    };










    constexpr std::string_view KEYWORDS[] =
    {
        "null",
        "true",
        "false",

        // Rest is type names
        "bool",
        "int",
        "uint",
        "float",

        "hex",
        "version",
        "string",
        "timestamp",

        "number",
        "number2",
        "number3",
        "number4",
        "number5",

        "int",
        "int2",
        "int3",
        "int4",
        "int5",

        "uint",
        "uint2",
        "uint3",
        "uint4",
        "uint5",

        "float",
        "float2",
        "float3",
        "float4",
        "float5",
    };
    constexpr size_t KEYWORD_COUNT = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

    enum class TokenType : uint8_t
    {
        NonExisting,  // Means the requested token doesn't exist/cannot be accessed, not necessarily an error
        Invalid,      // Means there is an error in the file content
        NewLine,
        EndOfFile,
        Comment,

        At,
        Equal,
        Comma,
        Colon,

        CurlyBraceOpen,
        CurlyBraceClose,
        SquareBraceOpen,
        SquareBraceClose,

        Identifier,

        Keyword,
        ValueLiteral_Begin = Keyword,
        IntLiteral,
        FloatLiteral,
        StringLiteral,
        HexLiteral,
        VersionLiteral,
        TimestampLiteral,

        EvaluateLiteral,
        ValueLiteral_End = EvaluateLiteral,
    };
    constexpr bool IsValueLiteral(TokenType type) noexcept
    { 
        return static_cast<uint8_t>(type) >= static_cast<uint8_t>(TokenType::ValueLiteral_Begin) &&
               static_cast<uint8_t>(type) <= static_cast<uint8_t>(TokenType::ValueLiteral_End);
    }
    struct Token
    {
        constexpr Token() noexcept = default;
        constexpr Token(TokenType type_, size_t startPosition_ = 0, size_t count_ = 0)
            : type(type_), startPosition(startPosition_), count(count_)  { }

        constexpr std::string_view ToView(const char* buffer)      const noexcept  { return std::string_view(buffer + startPosition, count); }
        constexpr std::string_view ToView(std::string_view buffer) const noexcept  { return buffer.substr(startPosition, count); }

        TokenType type = TokenType::NonExisting;
        uint8_t  extra8  = 0; // Token specific data, if needed
        uint16_t extra16 = 0; // Token specific data, if needed
        uint32_t extra32 = 0; // Token specific data, if needed
        size_t startPosition = 0;
        size_t count = 0;
    };




    template<std::size_t CAPACITY>
    struct TokenRingBuffer
    {
        static_assert(CAPACITY > 0, "RingBuffer capacity must be greater than 0!");
        constexpr TokenRingBuffer() noexcept = default;
        constexpr TokenRingBuffer(Token item) noexcept
        {
            assert(item.type != TokenType::NonExisting);
            currentToken = item;
        }

        constexpr bool Add(Token item) noexcept
        {
            assert(item.type != TokenType::NonExisting && currentToken.type != TokenType::NonExisting);

            if(IsFull())
                return false;

            this->operator[](size++) = (IsEmpty()? Current() : this->operator[](size - 1)).type == TokenType::EndOfFile? TokenType::EndOfFile : item;
            return true;
        }

        constexpr void Advance(size_t count = 0) noexcept
        {
            assert(size != 0 && count <= size);
            if(count == 0)
                return;

            currentToken = this->operator[](count - 1);

            // TODO: Loop is not necessary, for development purposes only
            for(size_t i = 0; i < count; i++)
                this->operator[](i) = {};

            size -= count;
            current = (current + count) % CAPACITY;
        }


        constexpr Token& operator[](std::size_t index) noexcept
        {
            assert(index < size);
            std::size_t actual_index = (current + index) % CAPACITY;
            return buffer[actual_index];
        }
        constexpr Token operator[](std::size_t index) const noexcept
        {
            assert(index < size);
            std::size_t actual_index = (current + index) % CAPACITY;
            return buffer[actual_index];
        }

        constexpr Token& Current()       noexcept  { return currentToken; }
        constexpr Token  Current() const noexcept  { return currentToken; }

        constexpr bool    IsEmpty() const noexcept  { return size == 0; }
        constexpr bool     IsFull() const noexcept  { return size == CAPACITY; }
        constexpr size_t     Size() const noexcept  { return size; }
        constexpr size_t Capacity() const noexcept  { return CAPACITY; }

    private:
        Token currentToken;
        Token buffer[CAPACITY] = {};
        size_t current = 0;
        size_t size = 0;
    };


    struct Tokenizer
    {
        static constexpr size_t PREVIEW_TOKEN_COUNT = 10;

        constexpr Tokenizer(std::string_view content_) noexcept
            : content(content_)
        {
            tokens = GetFirstToken();
        }

        constexpr void Reset(std::string_view content_) noexcept
        {
            content = content_;
            index = 0;
            tokenIndex = 0;
            tokens = GetFirstToken();
        }

        constexpr Token Current() const           noexcept  { return tokens.Current(); }
        constexpr void  Advance(size_t count = 1) noexcept  { tokens.Advance(count); }
        constexpr Token PeekAndAdvance()          noexcept  { Peek(); Advance(); return Current(); }

        constexpr Token Peek(size_t count = 1) noexcept
        {
            assert(count <= PREVIEW_TOKEN_COUNT);
            if(count == 0)
                return tokens.Current();

            if(tokens.Size() > count)
                return tokens[count - 1];

            for(size_t i = tokens.Size(); i < count; i++)
                tokens.Add(GetNextToken());

            return tokens[count - 1];
        }

        constexpr bool          HasMoreTokens() const noexcept  { return tokens.IsEmpty()? tokens.Current().type != TokenType::EndOfFile : tokens[tokens.Size() - 1].type != TokenType::EndOfFile; }
        constexpr size_t GetPreviewTokenCount() const noexcept  { return tokens.Size(); }
        constexpr size_t             GetIndex() const noexcept  { return index; }

    private:
        constexpr Token GetFirstToken() noexcept
        {
            /* Possible first tokens (excluding whitespace)
            *  Identifier - any identifier
            *  Keyword    - any keyword
            *  Comment    - //
            *  Comment    - /*
            *  At         - @
            *  NewLine    - \n
            */

            Token token = GetNextToken();
            if(token.type == TokenType::Identifier || token.type == TokenType::Keyword || token.type == TokenType::Comment || token.type == TokenType::At || token.type == TokenType::NewLine || token.type == TokenType::EndOfFile)
                return token;

            return TokenType::Invalid;
        }


        constexpr Token GetNextToken() noexcept
        {
            if(index >= content.size())
                return TokenType::EndOfFile;

            while(std::isspace(content[index]))
            {
                if(content[index] == '\n')
                {
                    Token token = Token(TokenType::NewLine, index);
                    while(index < content.size() && std::isspace(content[index]))
                    {
                        index++;
                        token.count++;
                    }

                    return token;
                }

                if(std::isspace(content[index]))
                    index++;

                if(index >= content.size())
                    return TokenType::EndOfFile;
            }



            if(content[index] == '\"' || content[index] == '\'')
            {
                size_t nextQuote = content.find_first_of(content[index], index + 1);
                if(nextQuote == std::string_view::npos)
                    return TokenType::Invalid;  // Non matching quotes

                while(content[nextQuote - 1] == '\\')
                {
                    nextQuote = content.find_first_of(content[index], nextQuote + 1);
                    if(nextQuote == std::string_view::npos)
                        return TokenType::Invalid;  // Non matching quotes
                }

                Token token = Token(TokenType::StringLiteral, index, nextQuote + 1 - index);
                index = nextQuote + 1;
                return token;
            }



            if(content[index] == '/')
            {
                if(index + 2 >= content.size())
                    return TokenType::Invalid; // not enough space for a comment

                if(content[index + 1] == '/') // single line comment
                {
                    size_t newLinePos = content.find_first_of('\n', index + 2);
                    Token token = Token(TokenType::Comment, content[index + 2] == ' '? index + 3 : index + 2);

                    if(newLinePos != std::string_view::npos)
                    {
                        token.count = newLinePos - token.startPosition;
                        index = newLinePos;
                        return token;
                    }

                    // There is no new lines left (comment is at the end of the file)
                    token.count = content.size() - token.startPosition;
                    index = -1;
                    return token;
                }

                if(content[index + 1] == '*') // multi line comment
                {
                    size_t slashPos = content.find_first_of('/', index + 2);
                    while(true)
                    {
                        if(slashPos == std::string_view::npos)
                            return TokenType::Invalid; // Non matching comment scope (There is only "/*" and not "*/")

                        if(content[slashPos - 1] == '*')
                        {
                            Token token = Token(TokenType::Comment, index + 2);
                            token.extra8 = 1;  // Means multi line
                            token.count = slashPos - 2 - token.startPosition;
                            index = slashPos + 1;
                            if(index + 1 < content.size() && content[index] == '\n')
                                index++;
                            return token;
                        }

                        slashPos = content.find_first_of('/', slashPos + 2);
                    }
                }

                return TokenType::Invalid;  // slash "/" without a comment
            }



            if(content[index] == '{')
                return Token(TokenType::CurlyBraceOpen, index++, 1);
            if(content[index] == '}')
                return Token(TokenType::CurlyBraceClose, index++, 1);
            if(content[index] == '[')
                return Token(TokenType::SquareBraceOpen, index++, 1);
            if(content[index] == ']')
                return Token(TokenType::SquareBraceClose, index++, 1);

            if(content[index] == '@')
                return Token(TokenType::At, index++, 1);
            if(content[index] == '=')
                return Token(TokenType::Equal, index++, 1);
            if(content[index] == ',')
                return Token(TokenType::Comma, index++, 1);
            if(content[index] == ':')
                return Token(TokenType::Colon, index++, 1);



            if(content[index] == '$')
            {
                if(index + 1 < content.size() && content[index + 1] == '{')
                {
                    size_t braceClose = content.find_first_of('}', index + 2);
                    if(braceClose == std::string_view::npos) // we reached eof before "}"
                        return TokenType::Invalid;

                    Token token = Token(TokenType::EvaluateLiteral, index, braceClose + 1 - index);
                    index = braceClose + 1;
                    return token;
                }

                return TokenType::Invalid; // Random dollar sign without "{"
            }



            if(std::isalpha(content[index]) || content[index] == '_') // identifier, keyword
            {
                Token token = Token(TokenType::Identifier, index);
                auto checkKeywords = [&](std::string_view view) -> void
                {
                    for(size_t i = 0; i < KEYWORD_COUNT; i++)
                    {
                        if(view == KEYWORDS[i])
                        {
                            token.type = TokenType::Keyword;
                            token.extra8 = i;  // Used as keyword index
                            return;
                        }
                    }
                };

                size_t firstNonAlpha = index + 1;
                while(firstNonAlpha < content.size() && (std::isalpha(content[firstNonAlpha]) || std::isdigit(content[firstNonAlpha]) || content[firstNonAlpha] == '_'))
                    firstNonAlpha++;

                token.count = firstNonAlpha - token.startPosition;
                std::string_view view = token.ToView(content);

                if(firstNonAlpha >= content.size()) // we reached eof before any space or any other token
                {
                    checkKeywords(view);
                    index = -1;
                    return token;
                }

                checkKeywords(view);
                index = firstNonAlpha;
                return token;
            }



            if(std::isdigit(content[index]) || content[index] == '-')
            {
                if(content[index] == '0' && index + 3 < content.size() && content[index + 1] == 'x')  // Hex
                {
                    size_t firstNonHex = content.find_first_not_of("0123456789abcdefABCDEF", index + 2);
                    size_t firstChar = content.find_first_of("abcdefABCDEF", index + 2);
                    size_t firstHash = content.find_first_of('#', index + 2);
                    if(firstNonHex == firstHash && firstNonHex != std::string_view::npos) // First non hex character is "#"
                    {
                        Token token = Token(TokenType::HexLiteral, index, firstNonHex - index + 1);
                        index = firstNonHex + 1;
                        return token;
                    }

                    if(firstNonHex == std::string_view::npos) // we reached eof before any space or any other token
                        return TokenType::Invalid;

                    if(firstChar < firstNonHex) // it contains hex characters, so we can't let it slide as a number
                        return TokenType::Invalid;

                    // Let it fallthrough as "multi dimensional int"
                }



                size_t firstNonDigit = content.find_first_not_of("0123456789", index + 1);
                if(firstNonDigit == std::string_view::npos)  // we reached eof before any space or any other token
                {
                    Token token = Token(TokenType::IntLiteral, index, content.size() - index);
                    token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc)
                    index = -1;
                    return token;
                }

                if(std::isspace(content[firstNonDigit]) || content[firstNonDigit] == ',')
                {
                    Token token = Token(TokenType::IntLiteral, index, firstNonDigit - index);
                    token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc)
                    index = firstNonDigit;
                    return token;
                }

                if(content[firstNonDigit] == '.')  // float, version or multi dimensional float
                {
                    Token token = Token(TokenType::FloatLiteral, index);
                    token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc)

                    size_t dotCount = 0;
                    size_t temp = firstNonDigit;
                    char lastChar = '.';
                    bool bContainsDash = false;

                    auto calculateResult = [&]() -> void
                    {
                        if(lastChar == '.' || lastChar == 'x')
                        {
                            token.type = TokenType::Invalid;  // Must end with a digit
                            return;
                        }

                        token.count = temp - token.startPosition;

                        if(dotCount == 2 || dotCount == 3)
                        {
                            if(bContainsDash)
                            {
                                token.type = TokenType::Invalid;  // Version cannot contain dash
                                return;
                            }

                            token.type = TokenType::VersionLiteral;
                            token.extra8 = dotCount + 1;
                            return;
                        }
                    };

                    while(temp <= content.size())
                    {
                        lastChar = content[temp];
                        if(std::isdigit(content[temp]) || (content[temp] == '-' && lastChar == 'x'))
                        {
                            if(content[temp] == '-')
                                bContainsDash = true;

                            temp++;
                            continue;
                        }

                        if(content[temp] == '.')
                        {
                            if(dotCount == 1 && token.extra8 > 1)
                                return TokenType::Invalid;  // Float can't have more than 1 dot
                            if(dotCount > 2)
                                return TokenType::Invalid;  // Version can have 3 dots maximum
                            
                            dotCount++;
                            temp++;
                            continue;
                        }

                        if(content[temp] == 'x')
                        {
                            dotCount = 0;
                            token.extra8++;
                            temp++;
                            continue;
                        }

                        if(std::isspace(content[temp]) || content[temp] == ',')
                        {
                            calculateResult();
                            index = temp;
                            return token;
                        }

                        return TokenType::Invalid; // Non allowed character
                    }

                    calculateResult();
                    index = -1;
                    return token;
                }

                if(content[firstNonDigit] == 'x')  // multi dimensional int
                {
                    Token token = Token(TokenType::IntLiteral, index);
                    token.extra8 = 2;  // Used as dimension (2d, 3d, 4d, 5d, etc)

                    size_t dotCount = 0;
                    while(true)
                    {
                        size_t previous = firstNonDigit;
                        firstNonDigit = content.find_first_not_of("0123456789", firstNonDigit + 1);

                        if(firstNonDigit == std::string_view::npos) // we reached eof before any space or any other token
                        {
                            token.count = content.size() - token.startPosition;
                            index = -1;
                            return token;
                        }

                        if(previous + 1 == firstNonDigit && !(content[previous] == ',' && std::isspace(content[firstNonDigit])) && !(content[previous] == 'x' && content[firstNonDigit] == '-'))
                            return TokenType::Invalid;  // It must have number(s) in between

                        if(std::isspace(content[firstNonDigit]) || content[firstNonDigit] == ',')
                        {
                            token.count = firstNonDigit - token.startPosition;
                            index = firstNonDigit;
                            auto v = token.ToView(content);
                            return token;
                        }

                        if(content[firstNonDigit] == 'x')
                        {
                            token.extra8++;
                            dotCount = 0;
                            continue;
                        }

                        if(content[firstNonDigit] == '.')
                        {
                            dotCount++;
                            if(dotCount > 1)
                                return TokenType::Invalid;  // Multi dimensional numbers can't contain more than 1 dot (for each number)

                            continue;
                        }
                    }
                }





                /* Possible datetime formats
                *  2024-12-24T15:30:00       -> Date + Time without timezone info (Usually interpreted as local time)
                *  2024-12-24T15:30:00Z      -> Date + Time with timezone info (Z means utc/zulu time)
                *  2024-12-24T15:30:00+05:30 -> Date + Time with timezone info (5 hours and 30 minutes ahead of UTC)
                *  2024-12-24                -> Date
                *  15:30:00                  -> Time
                *  2024-12-24T15:30:00.123Z  -> Date + Time with timezone info (Z means utc/zulu time) and milliseconds (123ms)
                *  2024-W52-2                -> Year + Week + Weekday (52nd week of 2024, tuesday)
                *  2024-359                  -> Year + Day of Year (359th day of 2024)
                */

                /* Possible duration formats (if we wanna support it, currently we don't) (Note: not here, it starts with a letter)
                *  P3D              -> 3 days
                *  P2W              -> 2 weeks (14 days)
                *  P1Y2M3D          -> 1 year, 2 months, 3 days
                *  P2WT3H           -> 2 weeks and 3 hours
                *  P5DT4H30M        -> 5 days, 4 hours, and 30 minutes
                *  PT1H45M          -> 1 hour and 45 minutes
                *  P1Y2M3DT4H30M10S -> 1 year, 2 months, 3 days, 4 hours, 30 minutes, and 10 seconds
                *  P10M             -> 10 minutes
                *  PT10M            -> 10 minutes (alternative representation for time)
                *  PT1.5S           -> 1.5 seconds (1 second and 500 milliseconds)
                *  PT0.000001S      -> 1 microsecond (0.000001 seconds)
                *  P1.5Y            -> 1.5 years (1 year and 6 months)
                *  P3DT5H30M        -> 3 days, 5 hours, and 30 minutes
                *  PT0.5H           -> 30 minutes (0.5 hours)
                *  P2Y3M4DT5H6M7S   -> 2 years, 3 months, 4 days, 5 hours, 6 minutes, and 7 seconds
                */


                if(content[firstNonDigit] == '-')  // date or datetime
                {
                    Token token = Token(TokenType::TimestampLiteral, index);
                    size_t firstNonDate = content.find_first_not_of("0123456789TZW-+:.", index);
                    if(firstNonDate == std::string_view::npos)
                    {
                        token.count = content.size() - token.startPosition;
                        index = -1;
                        return token;
                    }

                    if(std::isspace(content[firstNonDate]) || content[firstNonDate] == ',')
                    {
                        token.count = firstNonDate - token.startPosition;
                        index = firstNonDate;
                        return token;
                    }

                    return TokenType::Invalid;  // Invalid character after timestamp
                }

                if(content[firstNonDigit] == ':')  // time
                {
                    Token token = Token(TokenType::TimestampLiteral, index);
                    size_t firstNonDate = content.find_first_not_of("0123456789+:.", index);  // idk if it can include timezone ("+" sign)
                    if(firstNonDate == std::string_view::npos)
                    {
                        token.count = content.size() - token.startPosition;
                        index = -1;
                        return token;
                    }

                    if(std::isspace(content[firstNonDate]) || content[firstNonDate] == ',')
                    {
                        token.count = firstNonDate - token.startPosition;
                        index = firstNonDate;
                        return token;
                    }

                    return TokenType::Invalid;  // Invalid character after timestamp
                }

                return TokenType::Invalid;  // Something we didn't process yet?
            }

            return TokenType::Invalid;  // Something we didn't process yet?
        }

    private:
        std::string_view content;
        size_t index = 0;
        size_t tokenIndex = 0;
        TokenRingBuffer<PREVIEW_TOKEN_COUNT> tokens;
    };




    bool ParseFileContent(std::string_view content, std::vector<Entry>& entries, std::vector<Entry>& userTypes, std::string& fileComment) noexcept;
    bool ParseUserType(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& userTypes, Token comment);
    bool ParseVariable(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, const std::vector<Entry>& userTypes, Token comment, size_t parentEntryIndex);

    bool ParseDefaultValue(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, const std::vector<Entry>& userTypes, uint8_t typeID = 0, size_t userTypeID = -1);
    bool ParseSimpleValue(std::string_view content, Tokenizer& tokenizer, Entry& entry, uint8_t typeID);
    bool ParseArray(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, const std::vector<Entry>& userTypes, uint8_t typeID = 0, size_t userTypeID = -1);
    bool ParseMap(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, const std::vector<Entry>& userTypes, uint8_t typeID = 0, size_t userTypeID = -1);

    void CopyEntryDeep(std::vector<Entry>& target, const std::vector<Entry>& source, size_t sourceID);
    size_t FindEntryDeep(std::vector<Entry>& entries, std::string_view fullIdentifier, size_t depth = 0, size_t startIndex = 0);
    bool OverrideEntry(std::vector<Entry>& entries, size_t targetID, size_t sourceID);

    bool TestFiles();
}













FDF_EXPORT namespace fdf
{
    // TODO: When we manage our own memory in future, we intend to make Reader specifically tailored to be read only, so it's more efficient
    class Reader
    {
        friend class Writer;
        friend bool detail::TestFiles();
    public:
        Reader(std::string_view content) noexcept;
        Reader(std::filesystem::path filepath) noexcept;

        constexpr operator bool() const noexcept  { return bIsValid; }
        constexpr bool IsValid()  const noexcept  { return bIsValid; }

        Writer ToWriter() const;
        Writer MoveToWriter();
    
    private:
        bool bIsValid = false;
        std::string fileComment;
        std::vector<detail::Entry> entries;
        std::vector<detail::Entry> userTypes;
    };










    enum class CommentCombineStrategy : uint8_t
    {
        UseExisting,
        UseNew,
        UseNewIfExistingIsEmpty,
        Merge,
        Clear
    };
    class Writer
    {
    public:
        Writer() noexcept = default;
        Writer(const Reader& reader) noexcept
            : bIsValid(reader.IsValid()), entries(reader.entries), userTypes(reader.userTypes)  { }
        Writer(Reader&& reader) noexcept
            : bIsValid(reader.IsValid()), entries(std::move(reader.entries)), userTypes(std::move(reader.userTypes))  { }

        constexpr operator bool() const noexcept  { return bIsValid; }
        constexpr bool IsValid()  const noexcept  { return bIsValid; }

    public:
        Writer(std::string_view content) noexcept;
        Writer(std::filesystem::path filepath) noexcept;
        
        bool Combine(const Reader& other, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseExisting) noexcept;
        bool Combine(const Writer& other, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseExisting) noexcept;

    public:
        void WriteToBuffer(std::string& buffer) noexcept;
        void WriteToFile(std::filesystem::path filepath, bool bCreateIfNotExists = true) noexcept;

    public:
        bool Combine(std::string_view content, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseExisting) noexcept  
        { 
            Writer reader(content);
            return Combine(reader, fileCommentCombineStrategy);
        }
        bool Combine(std::filesystem::path filepath, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseExisting) noexcept 
        {
            Writer reader(filepath);
            return Combine(reader, fileCommentCombineStrategy);
        }

    public:
        // TODO: add API to get, add, remove, modify top level entries and access to their childs

    private:
        bool bIsValid = false;
        std::vector<detail::Entry> entries;
        std::vector<detail::Entry> userTypes;

    public:
        std::string fileComment;
    };
}
