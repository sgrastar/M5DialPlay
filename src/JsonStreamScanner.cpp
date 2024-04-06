#include "JsonStreamScanner.h"

int numberOfCharInString(String source, char search)
{
    int count = 0;
    int index = source.indexOf(search);
    while (index >= 0)
    {
        count++;
        if (index + 1 < source.length())
        {
            index = source.indexOf(search, index + 1);
        }
        else
        {
            break;
        }
    }
    return count;
}

JsonStreamScanner::JsonStreamScanner(Stream *stream)
{
    _stream = stream;
}

// Scan stream until ", and return path if it is key
String JsonStreamScanner::scanNextKey()
{
    while (_stream->available())
    {
        String word = _stream->readStringUntil('\"');
        int numberOfClose = numberOfCharInString(word, '}');
        int numberOfOpen = numberOfCharInString(word, '{');
        word.trim();

        // log_e("\"%s\", %d, %d", word.c_str(), numberOfOpen, numberOfClose);
        numberOfClose -= numberOfOpen;

        if (numberOfClose > 0)
        {
            for (int i = 0; i < numberOfClose; i++)
            {
                int lastIndex = _path.lastIndexOf("/");
                _path = _path.substring(0, lastIndex);
            }
            _push = false;
            _isValue = false;
        }
        else if (numberOfClose == 0 && numberOfOpen == 1)
        {
            _push = false;
            _isValue = false;
        }

        else if (numberOfClose == -1)
        {
            _push = true;
            _isValue = false;
        }

        else if (numberOfClose == 0 && numberOfOpen == 0)
        {
            if (word.indexOf(":") >= 0)
            {
                _push = false;
                _isValue = (word == ":");
                continue;
            }
            else if (word.indexOf(",") >= 0 || word.indexOf("[") >= 0 || word.indexOf("]") >= 0)
            {
                _push = false;
                _isValue = false;
                continue;
            }
            else
            {
                if (!_isValue)
                {
                    if (_push)
                    {
                        _path += "/" + word;
                    }
                    else
                    {
                        int lastIndex = _path.lastIndexOf("/");
                        _path = _path.substring(0, lastIndex);
                        _path += "/" + word;
                    }
                    _push = false;
                    _isValue = false;
                    return _path;
                }
            }
            _push = false;
            _isValue = false;
        }
        continue;
    }
    return "";
}

// Scan string value
String JsonStreamScanner::scanString()
{
    _stream->readStringUntil('\"');
    String word = _stream->readStringUntil('\"');
    return word;
}

// Scan boolean value
boolean JsonStreamScanner::scanBoolean()
{
    _stream->readStringUntil(':');
    String word = _stream->readStringUntil('e');
    word.trim();

    return (word.startsWith("tru"));
}

// Scan int value
long JsonStreamScanner::scanInt()
{
    _stream->readStringUntil(':');
    long number = _stream->parseInt();
    return number;
}

// Scan float value
float JsonStreamScanner::scanFloat()
{
    _stream->readStringUntil(':');
    float number = _stream->parseFloat();
    return number;
}

// Return current path
String JsonStreamScanner::path()
{
    return _path;
}

// Return if the stream is available
int JsonStreamScanner::available()
{
    return _stream->available();
}