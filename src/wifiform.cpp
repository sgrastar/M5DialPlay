#include "wifiform.h"

const String WiFiFormPart1 = R"(<!DOCTYPE HTML>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=480">
<style>
select {
    width: 80%;
    font-size: 24px;
}
input {
    width: 80%;
    font-size: 24px;
}
button {
    width: 80%;
    font-size: 24px;
}
</style>

<title>DialPlay WiFi settings</title>
</head>
<body>
<p>DialPlay WiFi settings</p>
<form action="/postwifi" method="POST">
<div>
<select name="SSID">
)";
// <option>選択肢1</option>
// <option>選択肢2</option>
// <option>選択肢3</option>
const String WiFiFormPart2 = R"(
</select>
</div>
<div>
    <label for="PASS">PASS</label>
    <input name="PASS" id="PASS" value="" />
</div>
<div>
    <button id="set">Set</button>
</div>
</form>

</body>
</html>
)";

const String autoCloseHtml = R"(<!DOCTYPE html>
<html>
<body onload="open(location, '_self').close();">
</body>
</html>)";

String htmlEscapedString(String sourceString)
{
    sourceString.replace("<", "&lt;");
    sourceString.replace(">", "&gt;");
    sourceString.replace("&", "&amp;");
    sourceString.replace("\"", "&quot;");
    sourceString.replace("'", "&#39;");
    sourceString.replace(" ", "&nbsp;");
    return sourceString;
}

