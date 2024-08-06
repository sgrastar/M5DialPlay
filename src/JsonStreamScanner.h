#ifndef JSONSTREAMSCANNER_H_INCLUDE
#define JSONSTREAMSCANNER_H_INCLUDE

#include <Arduino.h>

/*
JsonStreamScanner is a class to scan JSON data from desinated Stream.
*/

class JsonStreamScanner
{
public:
  JsonStreamScanner(Stream *stream, boolean chunked);
  String scanNextKey();
  String scanString();
  boolean scanBoolean();
  long scanInt();
  float scanFloat();

  String path();
  int available();

private:
  Stream *_stream;
  boolean _chunked;
  long _chunkSize;
  String _path = "";
  boolean _push = false;
  boolean _isValue = false;
};

#endif