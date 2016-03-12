# Cbin, a minimalist pastebin server

Cbin is a minimalist [pastebin](https://en.wikipedia.org/wiki/Pastebin).
It is written in C and only depends on the standard C library.
It can work as a [CGI](https://en.wikipedia.org/wiki/Common_Gateway_Interface) app,
or as a [HTTP daemon](https://en.wikipedia.org/wiki/Web_server) when started from [inetd](https://en.wikipedia.org/wiki/Inetd).
When browsing to the cbin app, a form will be returned with a text area and a submit button.
Users can type any text in the text area, up to 64 kilobytes in length.
When the submit button is clicked, the text is stored, and a URL will be returned with a 8 character random string
that will in turn display the stored text.
The URL itself is not stored anywhere, so every paste is a private one.
