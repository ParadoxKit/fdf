
#define FDF_CPP_FILE
#include "fdf.h"




namespace fdf::detail
{
    #define CHECK_TOKEN(TOKEN)              if(TOKEN.type == TokenType::Invalid    ) return false
    #define CHECK_TOKEN_FOR_EOF(TOKEN)      if(TOKEN.type == TokenType::EndOfFile  ) return false




    bool ParseFileContent(std::string_view content, std::vector<Entry>& entries, std::vector<Entry>& userTypes, std::string& fileComment) noexcept
    {
        Tokenizer tokenizer = content;
        Token currentComment = TokenType::NonExisting;
        while(true)
        {
            Token type = TokenType::NonExisting;
            Token currentToken = tokenizer.Current();
            CHECK_TOKEN(currentToken);

            while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
            {
                if(currentToken.type == TokenType::Comment)
                {
                    if(entries.empty() && fileComment.empty() && currentToken.count > 1 && content[currentToken.startPosition] == '*')
                    {
                        fileComment = currentToken.ToView(content);
                    }
                    else
                    {
                        // TODO: Maybe warn, if already has a comment?
                        currentComment = currentToken;
                    }
                }

                currentToken = tokenizer.Advance();
            }

            if(currentToken.type == TokenType::At)
            {
                Token currentToken = tokenizer.Advance();
                CHECK_TOKEN(currentToken);
                CHECK_TOKEN_FOR_EOF(currentToken);
                
                if(currentToken.type != TokenType::Identifier)
                    return false;

                if(!ParseUserType(content, tokenizer, userTypes, currentComment))
                    return false;

                continue;
            }

            if(currentToken.type == TokenType::Identifier)
            {
                if(!ParseVariable(content, tokenizer, entries, userTypes, currentComment, -1))
                    return false;

                continue;
            }
            
            if(currentToken.type == TokenType::EndOfFile)
                break;

            return false;  // First token can't be anything else
        }

        return true;
    }




    bool ParseUserType(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& userTypes, Token comment)
    {
        Entry& userType = userTypes.emplace_back();
        size_t currentUserTypeIndex = userTypes.size() - 1;

        userType.fullIdentifier = tokenizer.Current().ToView(content);
        userType.identifierSize = userType.fullIdentifier.size();

        if(comment.type != TokenType::NonExisting)
            userType.comment = comment.ToView(content);

        Token currentToken = tokenizer.Advance();
        CHECK_TOKEN(currentToken);
        CHECK_TOKEN_FOR_EOF(currentToken);



        if(currentToken.type == TokenType::Equal)
        {
            currentToken = tokenizer.Advance();
            CHECK_TOKEN(currentToken);
            CHECK_TOKEN_FOR_EOF(currentToken);
        }

        while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
        {
            if(currentToken.type == TokenType::Comment)
            {
                // TODO: Maybe warn, if already has a comment
                userType.comment = currentToken.ToView(content);
            }

            currentToken = tokenizer.Advance();
            CHECK_TOKEN(currentToken);
            CHECK_TOKEN_FOR_EOF(currentToken);
        }



        if(currentToken.type == TokenType::CurlyBraceOpen)
            return ParseMap(content, tokenizer, userTypes, userTypes, -1, currentUserTypeIndex) && OverrideEntry(userTypes, FindEntry(userTypes, userTypes[currentUserTypeIndex].fullIdentifier, userTypes[currentUserTypeIndex].depth, 0), currentUserTypeIndex);

        return false;
    }




    bool ParseVariable(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, const std::vector<Entry>& userTypes, Token comment, size_t parentEntryIndex, size_t currentlyProcessedUserTypeID)
    {
        const bool bHasParent    = parentEntryIndex != -1;
        const bool bArrayElement = bHasParent? entries[parentEntryIndex].type == Type::Array : false;
        Token currentToken = tokenizer.Current();

        Entry& entry = entries.emplace_back();
        size_t currentEntryIndex = entries.size() - 1;

        if(bArrayElement)
        {
            std::string temp = std::format("{}", entries[parentEntryIndex].size);
            entry.identifierSize = temp.size();
            entry.fullIdentifier = std::format("{}.{}", entries[parentEntryIndex].fullIdentifier, temp);
        }
        else
        {
            entry.fullIdentifier = currentToken.ToView(content);
            entry.identifierSize = entry.fullIdentifier.size();
            currentToken = tokenizer.Advance();
        }

        CHECK_TOKEN(currentToken);
        CHECK_TOKEN_FOR_EOF(currentToken);

        if(comment.type != TokenType::NonExisting)
            entry.comment = comment.ToView(content);

        if(bHasParent)
        {
            assert(parentEntryIndex < entries.size());
            Entry& parent = entries[parentEntryIndex];
            assert(parent.type == Type::Array || parent.type == Type::Map);

            entry.depth = parent.depth + 1;
            parent.size++;
            if(!bArrayElement)
                entry.fullIdentifier = parent.fullIdentifier + '.' + entry.fullIdentifier;
        }
        


        size_t userTypeID = -1;
        if(currentToken.type == TokenType::Colon)
        {
            currentToken = tokenizer.Advance();
            CHECK_TOKEN(currentToken);
            CHECK_TOKEN_FOR_EOF(currentToken);

            if(currentToken.type == TokenType::Keyword) // Builtin type (can still be Array or Map)
            {
                entry.typeID = currentToken.extra8;
                if(entry.typeID <= 2)
                    return false;  // These are value keywords, not types
            }
            else if(currentToken.type == TokenType::Identifier) // User type (Array or Map)
            {
                entry.typeID = 1;

                std::string_view userTypeName = currentToken.ToView(content);
                for(size_t i = 0; i < userTypes.size(); i++)
                {
                    if(userTypes[i].fullIdentifier == userTypeName)
                    {
                        userTypeID = i;
                        break;
                    }
                }

                if(userTypeID == -1)
                    return false;  // We didn't found the requested user type

                if(userTypeID == currentlyProcessedUserTypeID)
                    return false;  // Infinite recursion
            }
            else
            {
                return false;  // Unexpected token
            }


            currentToken = tokenizer.Advance();
            CHECK_TOKEN(currentToken);
            CHECK_TOKEN_FOR_EOF(currentToken);
        }

        

        bool bHasEqual = false;
        if(currentToken.type == TokenType::Equal)
        {
            bHasEqual = true;
            currentToken = tokenizer.Advance();
            CHECK_TOKEN(currentToken);
            CHECK_TOKEN_FOR_EOF(currentToken);
        }

        Token lastToken = TokenType::NonExisting;
        while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
        {
            if(currentToken.type == TokenType::Comment)
            {
                // TODO: Maybe warn, if already has a comment
                entry.comment = currentToken.ToView(content);
            }

            lastToken = currentToken;
            currentToken = tokenizer.Advance();
            CHECK_TOKEN(currentToken);
            if(bHasEqual)
                CHECK_TOKEN_FOR_EOF(currentToken);
        }



        if(IsValueLiteral(currentToken.type))
            return ParseSimpleValue(content, tokenizer, entry) && OverrideEntry(entries, FindEntry(entries, entries[currentEntryIndex].fullIdentifier, entries[currentEntryIndex].depth, bHasParent? parentEntryIndex : 0), currentEntryIndex);
        if(currentToken.type == TokenType::CurlyBraceOpen)
            return ParseMap(content, tokenizer, entries, userTypes, userTypeID) && OverrideEntry(entries, FindEntry(entries, entries[currentEntryIndex].fullIdentifier, entries[currentEntryIndex].depth, bHasParent? parentEntryIndex : 0), currentEntryIndex);
        if(currentToken.type == TokenType::SquareBraceOpen)
            return ParseArray(content, tokenizer, entries, userTypes, userTypeID) && OverrideEntry(entries, FindEntry(entries, entries[currentEntryIndex].fullIdentifier, entries[currentEntryIndex].depth, bHasParent? parentEntryIndex : 0), currentEntryIndex);
        if(!bHasEqual && lastToken.type == TokenType::NewLine)
            return ParseDefaultValue(content, tokenizer, entries, userTypes, userTypeID) && OverrideEntry(entries, FindEntry(entries, entries[currentEntryIndex].fullIdentifier, entries[currentEntryIndex].depth, bHasParent? parentEntryIndex : 0), currentEntryIndex);

        return false;  // Something we didn't process yet?
    }




    bool ParseDefaultValue(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, const std::vector<Entry>& userTypes, size_t userTypeID)
    {
        Entry& entry = entries[entries.size() - 1];

        switch(entry.typeID)
        {
            case 0: return false;  // You can't use default value without specifying a type
            case 1:
                CopyEntryDeep(entries, userTypes, userTypeID);
                return true;
            case 3:
                entry.type = Type::Bool;
                entry.size = 1;
                entry.data.b[0] = false;
                return true;
            case 4:
                entry.type = Type::Int;
                entry.size = 1;
                entry.data.i[0] = 0;
                return true;
            case 5:
                entry.type = Type::UInt;
                entry.size = 1;
                entry.data.u[0] = 0;
                return true;
            case 6:
                entry.type = Type::Float;
                entry.size = 1;
                entry.data.f[0] = 0.0;
                return true;
            case 7:
                entry.type = Type::Hex;
                entry.size = 0;
                entry.data.str[0] = '\0';
                return true;
            case 8:
                entry.type = Type::Version;
                entry.size = 3;
                entry.data.u[0] = 1;
                entry.data.u[1] = 0;
                entry.data.u[2] = 0;
                entry.data.u[3] = 0;
                return true;
            case 9:
                entry.type = Type::String;
                entry.size = 0;
                entry.data.str[0] = '\0';
                return true;
            case 10:
                entry.type = Type::Timestamp;
                entry.size = 0;
                entry.data.str[0] = '\0';
                return true;
            case 11:
            case 16:
                entry.type = Type::Int;
                entry.size = 1;
                entry.data.i[0] = 0;
                return true;
            case 12:
            case 17:
                entry.type = Type::Int;
                entry.size = 2;
                entry.data.i[0] = 0;
                entry.data.i[1] = 0;
                return true;
            case 13:
            case 18:
                entry.type = Type::Int;
                entry.size = 3;
                entry.data.i[0] = 0;
                entry.data.i[1] = 0;
                entry.data.i[2] = 0;
                return true;
            case 14:
            case 19:
                entry.type = Type::Int;
                entry.size = 4;
                entry.data.i[0] = 0;
                entry.data.i[1] = 0;
                entry.data.i[2] = 0;
                entry.data.i[3] = 0;
                return true;
            case 15:
            case 20:
                entry.type = Type::Int;
                entry.size = 5;
                entry.data.i[0] = 0;
                entry.data.i[1] = 0;
                entry.data.i[2] = 0;
                entry.data.i[3] = 0;
                entry.data.i[4] = 0;
                return true;
            case 21:
                entry.type = Type::UInt;
                entry.size = 1;
                entry.data.u[0] = 0;
                return true;
            case 22:
                entry.type = Type::UInt;
                entry.size = 2;
                entry.data.u[0] = 0;
                entry.data.u[1] = 0;
                return true;
            case 23:
                entry.type = Type::UInt;
                entry.size = 3;
                entry.data.u[0] = 0;
                entry.data.u[1] = 0;
                entry.data.u[2] = 0;
                return true;
            case 24:
                entry.type = Type::UInt;
                entry.size = 4;
                entry.data.u[0] = 0;
                entry.data.u[1] = 0;
                entry.data.u[2] = 0;
                entry.data.u[3] = 0;
                return true;
            case 25:
                entry.type = Type::UInt;
                entry.size = 5;
                entry.data.u[0] = 0;
                entry.data.u[1] = 0;
                entry.data.u[2] = 0;
                entry.data.u[3] = 0;
                entry.data.u[4] = 0;
                return true;
            case 26:
                entry.type = Type::Float;
                entry.size = 1;
                entry.data.f[0] = 0;
                return true;
            case 27:
                entry.type = Type::Float;
                entry.size = 2;
                entry.data.f[0] = 0;
                entry.data.f[1] = 0;
                return true;
            case 28:
                entry.type = Type::Float;
                entry.size = 3;
                entry.data.f[0] = 0;
                entry.data.f[1] = 0;
                entry.data.f[2] = 0;
                return true;
            case 29:
                entry.type = Type::Float;
                entry.size = 4;
                entry.data.f[0] = 0;
                entry.data.f[1] = 0;
                entry.data.f[2] = 0;
                entry.data.f[3] = 0;
                return true;
            case 30:
                entry.type = Type::Float;
                entry.size = 5;
                entry.data.f[0] = 0;
                entry.data.f[1] = 0;
                entry.data.f[2] = 0;
                entry.data.f[3] = 0;
                entry.data.f[4] = 0;
                return true;
            default: return false;  // Something we didn't process yet?
        }
    }




    bool ParseSimpleValue(std::string_view content, Tokenizer& tokenizer, Entry& entry)
    {
        Token currentToken = tokenizer.Current();
        std::string_view view = currentToken.ToView(content);

        auto postProcess = [&]()
        {
            currentToken = tokenizer.Advance();
            if(currentToken.type == TokenType::Comment)
            {
                // TODO: Maybe warn, if already has a comment?
                entry.comment = currentToken.ToView(content);
                currentToken = tokenizer.Advance();
            }

            if(currentToken.type == TokenType::NewLine)
                tokenizer.Advance();
        };
        
        if(currentToken.type == TokenType::Keyword)
        {
            if(currentToken.extra8 == 0)
            {
                if(entry.typeID == 0)
                {
                    entry.type = Type::Null;
                    postProcess();
                    return true;
                }
                
                return false;  // TODO: For now, we don't allow null value for explicitly specified types
            }
            if(currentToken.extra8 == 1 || currentToken.extra8 == 2)
            {
                entry.type = Type::Bool;
                entry.size = 1;
                entry.data.b[0] = currentToken.extra8 == 1;
                postProcess();
                return true;
            }

            return false;  // Invalid keyword when expected a value
        }

        if(entry.typeID == 1)
            return false;  // User type cannot have a simple value literal

        entry.size = currentToken.extra8;  // Dimension count or string length




        if(currentToken.type == TokenType::IntLiteral)
        {
            bool bIsUnsigned = false;
            bool bContainsAnyNegative = false;
            bool bIsFirstChar = true;
            bool bIsNegative = false;

            uint64_t result = 0;
            uint8_t currentDimension = 0;
            const uint8_t dimensionCount = entry.size;

            auto finishDimension = [&]() -> bool
            {
                if(bIsNegative)
                {
                    if(bIsUnsigned || result > INT64_MAX_VALUE)
                        return false;

                    entry.data.i[currentDimension] = -result;
                }
                else
                {
                    bool bWasUnsigned = bIsUnsigned;
                    if(result > INT64_MAX_VALUE)
                        bIsUnsigned = true;

                    if(bIsUnsigned)
                    {
                        if(bContainsAnyNegative)
                            return false;

                        if(!bWasUnsigned)
                        {
                            int64_t temp[VARIANT_64BIT_ELEMENT_COUNT];
                            for(uint8_t i = 0; i < currentDimension - 1; i++)
                                temp[i] = entry.data.i[i];

                            for(uint8_t i = 0; i < currentDimension - 1; i++)
                            {
                                if(temp[i] > INT64_MAX_VALUE)
                                    return false;

                                entry.data.u[i] = temp[i];
                            }
                        }

                        entry.data.u[currentDimension] = result;
                    }
                    else
                        entry.data.i[currentDimension] = result;
                }

                return true;
            };


            for(size_t i = 0; i < view.size(); i++)
            {
                char c = view[i];
                if(bIsFirstChar && c == '-')
                {
                    bIsNegative = true;
                    bContainsAnyNegative = true;
                }
                else if(std::isdigit(c))
                {
                    if(result > UINT64_MAX_VALUE / 10)
                        return false;  // Overflow

                    result *= 10;

                    const uint64_t digit = c - '0';
                    if(result > UINT64_MAX_VALUE - digit)
                        return false; // Overflow

                    result += digit;
                }
                else if(c == '.')
                {
                    while(i < view.size() && (view[i] == '.' || std::isdigit(view[i])))
                        i++;
                }
                else if(c == 'x')
                {
                    if(currentDimension >= dimensionCount - 1)
                        return false;  // Too much dimensions

                    if(!finishDimension())
                        return false;

                    bIsFirstChar = true;
                    bIsNegative = false;

                    result = 0;
                    currentDimension++;

                    continue;
                }
                else
                    return false;  // unknown character

                bIsFirstChar = false;
            }

            if(!finishDimension())
                return false;

            entry.type = bIsUnsigned? Type::UInt : Type::Int;
            postProcess();
            return true;
        }




        if(currentToken.type == TokenType::FloatLiteral)
        {
            entry.type = Type::Float;

            bool bIsFirstChar = true;
            bool bIsNegative = false;
            bool bAfterDot = false;

            double multiplier = 1.0;
            double result = 0.0;
            uint8_t currentDimension = 0;
            const uint8_t dimensionCount = entry.size;

            for(size_t i = 0; i < view.size(); i++)
            {
                char c = view[i];
                if(bIsFirstChar && c == '-')
                {
                    bIsNegative = true;
                }
                else if(std::isdigit(c))
                {
                    if(result > DOUBLE_MAX_VALUE / 10)
                        return false;  // Overflow

                    if(!bAfterDot)
                        result *= 10;

                    const double value = static_cast<double>(c - '0') * multiplier;
                    if(result > DOUBLE_MAX_VALUE - value)
                        return false; // Overflow

                    result += value;
                }
                else if(c == '.')
                {
                    if(bAfterDot)
                        return false;  // Can't contain multiple dots

                    bAfterDot = true;
                }
                else if(c == 'x')
                {
                    if(currentDimension >= dimensionCount - 1)
                        return false;  // Too much dimensions

                    entry.data.f[currentDimension] = bIsNegative? -result : result;

                    bIsFirstChar = true;
                    bIsNegative = false;
                    bAfterDot = false;

                    multiplier = 1.0;
                    result = 0.0;
                    currentDimension++;

                    continue;
                }
                else
                    return false;  // unknown character

                bIsFirstChar = false;
                if(bAfterDot)
                    multiplier *= 0.1;
            }

            entry.data.f[currentDimension] = bIsNegative? -result : result;
            postProcess();
            return true;
        }




        if(currentToken.type == TokenType::VersionLiteral)
        {
            entry.type = Type::Version;
            entry.data.u[3] = 0;

            uint8_t currentDimension = 0;
            const uint8_t dimensionCount = entry.size;

            uint64_t result = 0;
            for(size_t i = 0; i < view.size(); i++)
            {
                char c = view[i];
                if(std::isdigit(c))
                {
                    if(result > UINT64_MAX_VALUE / 10)
                        return false;  // Overflow

                    result *= 10;

                    const uint64_t digit = c - '0';
                    if(result > UINT64_MAX_VALUE - digit)
                        return false; // Overflow

                    result += digit;
                }
                else if(c == '.')
                {
                    if(currentDimension >= dimensionCount - 1)
                        return false;  // Too much dimensions

                    entry.data.u[currentDimension] = result;

                    result = 0;
                    currentDimension++;
                }
                else
                    return false;  // unknown character
            }

            entry.data.u[currentDimension] = result;
            postProcess();
            return true;
        }



        
        size_t size = 0;
        bool bDynamic = false;
        auto writeCharacter = [&](char c)
        {
            if(size < VARIANT_SIZE)
            {
                entry.data.str[size++] = c;
            }
            else
            {
                if(!bDynamic)
                {
                    bDynamic = true;
                    char buffer[VARIANT_SIZE] = {};
                    memcpy(buffer, entry.data.str, size);
                    entry.data.strDynamic.InitialAllocate(entry.size + 1);
                    memcpy(entry.data.strDynamic.data, buffer, size);
                }

                entry.data.strDynamic[size++] = c;
            }
        };
        auto writeDynamicCharacter = [&](char c)
        {
            entry.data.strDynamic[size++] = c;
        };

        if(currentToken.type == TokenType::StringLiteral)
        {
            entry.size = view.size() - 2;
            entry.type = Type::String;

            size_t escapeCharacters = 0;
            if(view.size() < VARIANT_DYNAMIC_STRING_HARD_LIMIT)
            {
                for(int i = 1; i <= entry.size; i++)
                {
                    if(view[i + escapeCharacters] == '\\')
                    {
                        entry.size--;
                        escapeCharacters++;
                    }

                    writeCharacter(view[i + escapeCharacters]);
                }
                writeCharacter('\0');
            }
            else
            {
                entry.data.strDynamic.InitialAllocate(entry.size + 1);
                for(int i = 1; i <= entry.size; i++)
                {
                    if(view[i + escapeCharacters] == '\\')
                    {
                        entry.size--;
                        escapeCharacters++;
                    }

                    writeDynamicCharacter(view[i + escapeCharacters]);
                }

                writeDynamicCharacter('\0');
                entry.data.strDynamic.RefreshView();
            }

            postProcess();
            return true;
        }




        if(currentToken.type == TokenType::HexLiteral || currentToken.type == TokenType::TimestampLiteral)
        {
            entry.size = view.size();
            entry.type = currentToken.type == TokenType::HexLiteral? Type::Hex : Type::Timestamp;

            if(view.size() + 1 < VARIANT_SIZE)
            {
                memcpy(entry.data.str, view.data(), view.size());
                entry.data.str[view.size()] = '\0';
            }
            else
            {
                entry.data.strDynamic.InitialAllocate(entry.size + 1);
                memcpy(entry.data.strDynamic.data, view.data(), view.size());
                entry.data.strDynamic.data[view.size()] = '\0';
                entry.data.strDynamic.RefreshView();
            }

            postProcess();
            return true;
        }




        if(currentToken.type == TokenType::EvaluateLiteral)
        {
            entry.size = EVALUATE_LITERAL_TEXT.size();
            entry.type = Type::String;
            memcpy(entry.data.str, EVALUATE_LITERAL_TEXT.data(), EVALUATE_LITERAL_TEXT.size());

            postProcess();
            return true;
        }

        return false;  // Something we didn't process yet?
    }




    bool ParseArray(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, const std::vector<Entry>& userTypes, size_t userTypeID)
    {
        size_t entryID = entries.size() - 1;
        entries[entryID].type = Type::Array;

        Token currentToken = tokenizer.Advance();
        CHECK_TOKEN(currentToken);
        CHECK_TOKEN_FOR_EOF(currentToken);

        while(true)
        {
            Token comment = TokenType::NonExisting;
            while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
            {
                if(currentToken.type == TokenType::Comment)
                    comment = currentToken;

                currentToken = tokenizer.Advance();
                CHECK_TOKEN(currentToken);
                CHECK_TOKEN_FOR_EOF(currentToken);
            }


            if(IsValueLiteral(currentToken.type) || currentToken.type == TokenType::CurlyBraceOpen || currentToken.type == TokenType::SquareBraceOpen || currentToken.type == TokenType::Colon)
            {
                if(!ParseVariable(content, tokenizer, entries, userTypes, comment, entryID))
                    return false;

                currentToken = tokenizer.Current();
                if(currentToken.type == TokenType::Comma)
                {
                    currentToken = tokenizer.Advance();
                    CHECK_TOKEN(currentToken);
                    CHECK_TOKEN_FOR_EOF(currentToken);
                }
            }
            else if(currentToken.type == TokenType::SquareBraceClose)
            {
                currentToken = tokenizer.Advance();
                CHECK_TOKEN(currentToken);

                if(currentToken.type == TokenType::NewLine)
                {
                    currentToken = tokenizer.Advance();
                    CHECK_TOKEN(currentToken);
                }
                return true;
            }
            else
                return false;
        }
    }




    bool ParseMap(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, const std::vector<Entry>& userTypes, size_t userTypeID, size_t currentlyProcessedUserTypeID)
    {
        size_t entryID = entries.size() - 1;
        entries[entryID].type = Type::Map;

        if(entries[entryID].typeID == 1)
            CopyEntryDeep(entries, userTypes, userTypeID);

        Token currentToken = tokenizer.Advance();
        CHECK_TOKEN(currentToken);
        CHECK_TOKEN_FOR_EOF(currentToken);

        while(true)
        {
            Token comment = TokenType::NonExisting;
            while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
            {
                if(currentToken.type == TokenType::Comment)
                    comment = currentToken;

                currentToken = tokenizer.Advance();
                CHECK_TOKEN(currentToken);
                CHECK_TOKEN_FOR_EOF(currentToken);
            }


            if(currentToken.type == TokenType::Identifier)
            {
                if(!ParseVariable(content, tokenizer, entries, userTypes, comment, entryID, currentlyProcessedUserTypeID))
                    return false;

                currentToken = tokenizer.Current();
                if(currentToken.type == TokenType::Comma)
                {
                    currentToken = tokenizer.Advance();
                    CHECK_TOKEN(currentToken);
                    CHECK_TOKEN_FOR_EOF(currentToken);
                }
            }
            else if(currentToken.type == TokenType::CurlyBraceClose)
            {
                currentToken = tokenizer.Advance();
                CHECK_TOKEN(currentToken);

                if(currentToken.type == TokenType::NewLine)
                {
                    currentToken = tokenizer.Advance();
                    CHECK_TOKEN(currentToken);
                }
                return true;
            }
            else
                return false;
        }
    }




    size_t FindEntry(std::vector<Entry>& entries, std::string_view fullIdentifier, size_t depth, size_t startIndex)
    {
        for(size_t i = startIndex; i < entries.size(); i++)
        {
            if(entries[i].depth == depth && entries[i].fullIdentifier == fullIdentifier)
                return i;
        }

        return -1;
    }

    void CopyEntryDeep(std::vector<Entry>& target, const std::vector<Entry>& source, size_t sourceID)
    {
        size_t targetID = target.size() - 1;

        target[targetID].type = source[sourceID].type;
        target[targetID].size = source[sourceID].size;

        for(size_t i = 0; i < source[sourceID].size; i++)
        {
            const size_t index = sourceID + i + 1;
            Entry& last = target.emplace_back(source[index]);
            last.depth += target[targetID].depth;
            last.fullIdentifier = std::format("{}{}", target[targetID].fullIdentifier, source[index].GetIdentifierWithDot());
            if(last.type == Type::Array || last.type == Type::Map)
                CopyEntryDeep(target, source, index);
        }
    }

    bool OverrideEntry(std::vector<Entry>& entries, size_t targetID, size_t sourceID)
    {
        if(targetID == sourceID)
            return true;

        if(entries[targetID].type == entries[sourceID].type)
        {
            if(entries[targetID].type != Type::Array && entries[targetID].type != Type::Map)
            {
                entries[targetID] = entries[sourceID];
                entries.pop_back();
                return true;
            }
            else
            {
                // TODO: handle Array and Map
                // TODO: Just override every child and their childs and so on... Don't bother with merging or something like that
                return false;
            }
        }

        return false;  // Something we didn't process yet?
    }
}










namespace fdf
{
    Reader::Reader(std::string_view content) noexcept
    {
        if(detail::ParseFileContent(content, entries, userTypes, fileComment))
            bIsValid = true;
    }

    Reader::Reader(std::filesystem::path filepath) noexcept
    {
        if(!std::filesystem::exists(filepath) || !std::filesystem::is_regular_file(filepath))
            return;

        std::ifstream file(filepath);
        if(file)
        {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if(detail::ParseFileContent(content, entries, userTypes, fileComment))
                bIsValid = true;
        }
    }

    Writer Reader::ToWriter() const
    {
        return *this;
    }
    Writer Reader::MoveToWriter()
    {
        return std::move(*this);
    }
}










namespace fdf
{
    Writer::Writer(std::string_view content) noexcept
    {
        if(detail::ParseFileContent(content, entries, userTypes, fileComment))
            bIsValid = true;
    }

    Writer::Writer(std::filesystem::path filepath) noexcept
    {
        if(!std::filesystem::exists(filepath) || !std::filesystem::is_regular_file(filepath))
            return;

        std::ifstream file(filepath);
        if(file)
        {
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if(detail::ParseFileContent(content, entries, userTypes, fileComment))
                bIsValid = true;
        }
    }

    bool Writer::Combine(const Reader& other, CommentCombineStrategy fileCommentCombineStrategy) noexcept
    {
        if(!IsValid() || !other.IsValid())
            return false;

        switch(fileCommentCombineStrategy)
        {
            case CommentCombineStrategy::UseExisting: break;
            case CommentCombineStrategy::UseNew: fileComment = other.fileComment; break;
            case CommentCombineStrategy::UseNewIfExistingIsEmpty: 
                if(fileComment.empty())
                    fileComment = other.fileComment;
                break;
            case CommentCombineStrategy::Merge:
                if(fileComment.empty())
                    fileComment = other.fileComment;
                else if(!other.fileComment.empty())
                    fileComment = fileComment + '\n' + other.fileComment;
                break;
            case CommentCombineStrategy::Clear: fileComment.clear(); break;
            default: assert(false);
        }

        entries.insert_range(entries.end(), other.entries);
        userTypes.insert_range(userTypes.end(), other.userTypes);
        return true;
    }

    bool Writer::Combine(const Writer& other, CommentCombineStrategy fileCommentCombineStrategy) noexcept
    {
        if(!IsValid() || !other.IsValid())
            return false;

        switch(fileCommentCombineStrategy)
        {
            case CommentCombineStrategy::UseExisting: break;
            case CommentCombineStrategy::UseNew: fileComment = other.fileComment; break;
            case CommentCombineStrategy::UseNewIfExistingIsEmpty: 
                if(fileComment.empty())
                    fileComment = other.fileComment;
                break;
            case CommentCombineStrategy::Merge:
                if(fileComment.empty())
                    fileComment = other.fileComment;
                else if(!other.fileComment.empty())
                    fileComment = fileComment + '\n' + other.fileComment;
                break;
            case CommentCombineStrategy::Clear: fileComment.clear(); break;
            default: assert(false);
        }

        entries.insert_range(entries.end(), other.entries);
        userTypes.insert_range(userTypes.end(), other.userTypes);
        return true;
    }


    void Writer::WriteToBuffer(std::string& buffer) noexcept
    {

    }
    void Writer::WriteToFile(std::filesystem::path filepath, bool bCreateIfNotExists) noexcept
    {

    }
}
