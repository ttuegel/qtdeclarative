/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the QtDeclarative module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qdeclarativejslexer_p.h"
#include "qdeclarativejsengine_p.h"
#include "qdeclarativejsnodepool_p.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>

QT_BEGIN_NAMESPACE
Q_CORE_EXPORT double qstrtod(const char *s00, char const **se, bool *ok);
QT_END_NAMESPACE

using namespace QDeclarativeJS;

enum RegExpFlag {
    Global     = 0x01,
    IgnoreCase = 0x02,
    Multiline  = 0x04
};

static int flagFromChar(const QChar &ch)
{
    switch (ch.unicode()) {
    case 'g': return Global;
    case 'i': return IgnoreCase;
    case 'm': return Multiline;
    }
    return 0;
}

static unsigned char convertHex(ushort c)
{
    if (c >= '0' && c <= '9')
        return (c - '0');
    else if (c >= 'a' && c <= 'f')
        return (c - 'a' + 10);
    else
        return (c - 'A' + 10);
}

static unsigned char convertHex(QChar c1, QChar c2)
{
    return ((convertHex(c1.unicode()) << 4) + convertHex(c2.unicode()));
}

static QChar convertUnicode(QChar c1, QChar c2, QChar c3, QChar c4)
{
    return QChar((convertHex(c3.unicode()) << 4) + convertHex(c4.unicode()),
                 (convertHex(c1.unicode()) << 4) + convertHex(c2.unicode()));
}

Lexer::Lexer(Engine *engine)
    : _engine(engine)
    , _codePtr(0)
    , _lastLinePtr(0)
    , _tokenLinePtr(0)
    , _tokenStartPtr(0)
    , _char(QLatin1Char('\n'))
    , _errorCode(NoError)
    , _currentLineNumber(0)
    , _tokenValue(0)
    , _parenthesesState(IgnoreParentheses)
    , _parenthesesCount(0)
    , _stackToken(-1)
    , _patternFlags(0)
    , _tokenLength(0)
    , _tokenLine(0)
    , _validTokenText(false)
    , _prohibitAutomaticSemicolon(false)
    , _restrictedKeyword(false)
    , _terminator(false)
    , _delimited(false)
{
    if (engine)
        engine->setLexer(this);
}

QString Lexer::code() const
{
    return _code;
}

void Lexer::setCode(const QString &code, int lineno)
{
    if (_engine)
        _engine->setCode(code);

    _code = code;
    _tokenText.clear();
    _errorMessage.clear();
    _tokenSpell = QStringRef();

    _codePtr = code.unicode();
    _lastLinePtr = _codePtr;
    _tokenLinePtr = _codePtr;
    _tokenStartPtr = _codePtr;

    _char = QLatin1Char('\n');
    _errorCode = NoError;

    _currentLineNumber = lineno;
    _tokenValue = 0;

    // parentheses state
    _parenthesesState = IgnoreParentheses;
    _parenthesesCount = 0;

    _stackToken = -1;

    _patternFlags = 0;
    _tokenLength = 0;
    _tokenLine = lineno;

    _validTokenText = false;
    _prohibitAutomaticSemicolon = false;
    _restrictedKeyword = false;
    _terminator = false;
    _delimited = false;
}

void Lexer::scanChar()
{
    _char = *_codePtr++;

    if (_char == QLatin1Char('\n')) {
        _lastLinePtr = _codePtr; // points to the first character after the newline
        ++_currentLineNumber;
    }
}

int Lexer::lex()
{
    _tokenSpell = QStringRef();
    int token = scanToken();
    _tokenLength = _codePtr - _tokenStartPtr - 1;

    _delimited = false;
    _restrictedKeyword = false;

    // update the flags
    switch (token) {
    case T_LBRACE:
    case T_SEMICOLON:
        _delimited = true;
        break;

    case T_IF:
    case T_FOR:
    case T_WHILE:
    case T_WITH:
        _parenthesesState = CountParentheses;
        _parenthesesCount = 0;
        break;

    case T_DO:
        _parenthesesState = BalancedParentheses;
        break;

    case T_CONTINUE:
    case T_BREAK:
    case T_RETURN:
    case T_THROW:
        _restrictedKeyword = true;
        break;
    } // switch

    // update the parentheses state
    switch (_parenthesesState) {
    case IgnoreParentheses:
        break;

    case CountParentheses:
        if (token == T_RPAREN) {
            --_parenthesesCount;
            if (_parenthesesCount == 0)
                _parenthesesState = BalancedParentheses;
        } else if (token == T_LPAREN) {
            ++_parenthesesCount;
        }
        break;

    case BalancedParentheses:
        _parenthesesState = IgnoreParentheses;
        break;
    } // switch

    return token;
}

bool Lexer::isUnicodeEscapeSequence(const QChar *chars)
{
    if (isHexDigit(chars[0]) && isHexDigit(chars[1]) && isHexDigit(chars[2]) && isHexDigit(chars[3]))
        return true;

    return false;
}

QChar Lexer::decodeUnicodeEscapeCharacter(bool *ok)
{
    if (_char == QLatin1Char('u') && isUnicodeEscapeSequence(&_codePtr[0])) {
        scanChar(); // skip u

        const QChar c1 = _char;
        scanChar();

        const QChar c2 = _char;
        scanChar();

        const QChar c3 = _char;
        scanChar();

        const QChar c4 = _char;
        scanChar();

        if (ok)
            *ok = true;

        return convertUnicode(c1, c2, c3, c4);
    }

    *ok = false;
    return QChar();
}

int Lexer::scanToken()
{
    if (_stackToken != -1) {
        int tk = _stackToken;
        _stackToken = -1;
        return tk;
    }

    _terminator = false;

again:
    _validTokenText = false;
    _tokenLinePtr = _lastLinePtr;

    while (_char.isSpace()) {
        if (_char == QLatin1Char('\n')) {
            _tokenLinePtr = _codePtr;

            if (_restrictedKeyword) {
                // automatic semicolon insertion
                _tokenLine = _currentLineNumber;
                _tokenStartPtr = _codePtr - 1; // ### TODO: insert it before the optional \r sequence.
                return T_SEMICOLON;
            } else {
                _terminator = true;
                syncProhibitAutomaticSemicolon();
            }
        }

        scanChar();
    }

    _tokenStartPtr = _codePtr - 1;
    _tokenLine = _currentLineNumber;

    if (_char.isNull())
        return EOF_SYMBOL;

    const QChar ch = _char;
    scanChar();

    switch (ch.unicode()) {
    case '~': return T_TILDE;
    case '}': return T_RBRACE;

    case '|':
        if (_char == QLatin1Char('|')) {
            scanChar();
            return T_OR_OR;
        } else if (_char == QLatin1Char('=')) {
            scanChar();
            return T_OR_EQ;
        }
        return T_OR;

    case '{': return T_LBRACE;

    case '^':
        if (_char == QLatin1Char('=')) {
            scanChar();
            return T_XOR_EQ;
        }
        return T_XOR;

    case ']': return T_RBRACKET;
    case '[': return T_LBRACKET;
    case '?': return T_QUESTION;

    case '>':
        if (_char == QLatin1Char('>')) {
            scanChar();
            if (_char == QLatin1Char('>')) {
                scanChar();
                if (_char == QLatin1Char('=')) {
                    scanChar();
                    return T_GT_GT_GT_EQ;
                }
                return T_GT_GT_GT;
            } else if (_char == QLatin1Char('=')) {
                scanChar();
                return T_GT_GT_EQ;
            }
            return T_GT_GT;
        } else if (_char == QLatin1Char('=')) {
            scanChar();
            return T_GE;
        }
        return T_GT;

    case '=':
        if (_char == QLatin1Char('=')) {
            scanChar();
            if (_char == QLatin1Char('=')) {
                scanChar();
                return T_EQ_EQ_EQ;
            }
            return T_EQ_EQ;
        }
        return T_EQ;

    case '<':
        if (_char == QLatin1Char('=')) {
            scanChar();
            return T_LE;
        } else if (_char == QLatin1Char('<')) {
            scanChar();
            if (_char == QLatin1Char('=')) {
                scanChar();
                return T_LT_LT_EQ;
            }
            return T_LT_LT;
        }
        return T_LT;

    case ';': return T_SEMICOLON;
    case ':': return T_COLON;

    case '/':
        if (_char == QLatin1Char('*')) {
            scanChar();
            while (!_char.isNull()) {
                if (_char == QLatin1Char('*')) {
                    scanChar();
                    if (_char == QLatin1Char('/')) {
                        scanChar();
                        goto again;
                    }
                } else {
                    scanChar();
                }
            }
        } else if (_char == QLatin1Char('/')) {
            while (!_char.isNull() && _char != QLatin1Char('\n')) {
                scanChar();
            }
            goto again;
        } if (_char == QLatin1Char('=')) {
            scanChar();
            return T_DIVIDE_EQ;
        }
        return T_DIVIDE_;

    case '.':
        if (_char.isDigit()) {
            QByteArray chars;
            chars.reserve(32);
            while (_char.isLetterOrNumber() || _char == QLatin1Char('.')) {
                if (_char == QLatin1Char('e') || _char == QLatin1Char('E')) {
                    chars += _char.unicode();
                    scanChar(); // skip e

                    if (_char == QLatin1Char('+') || _char == QLatin1Char('-')) {
                        chars += _char.unicode();
                        scanChar(); // skip +/-
                    }
                } else {
                    chars += _char.unicode();
                    scanChar();
                }
            }
            const char *begin = chars.constData();
            const char *end = 0;
            bool ok = false;
            _tokenValue = qstrtod(begin, &end, &ok);

            if (! ok || end != chars.end()) {
                _errorCode = IllegalExponentIndicator;
                _errorMessage = QCoreApplication::translate("QDeclarativeParser", "Illegal syntax for exponential number");
                return T_ERROR;
            }

            return T_NUMERIC_LITERAL;
        }
        return T_DOT;

    case '-':
        if (_char == QLatin1Char('=')) {
            scanChar();
            return T_MINUS_EQ;
        } else if (_char == QLatin1Char('-')) {
            scanChar();

            if (_terminator && !_delimited && !_prohibitAutomaticSemicolon) {
                _stackToken = T_PLUS_PLUS;
                return T_SEMICOLON;
            }

            return T_MINUS_MINUS;
        }
        return T_MINUS;

    case ',': return T_COMMA;

    case '+':
        if (_char == QLatin1Char('=')) {
            scanChar();
            return T_PLUS_EQ;
        } else if (_char == QLatin1Char('+')) {
            scanChar();

            if (_terminator && !_delimited && !_prohibitAutomaticSemicolon) {
                _stackToken = T_PLUS_PLUS;
                return T_SEMICOLON;
            }

            return T_PLUS_PLUS;
        }
        return T_PLUS;

    case '*':
        if (_char == QLatin1Char('=')) {
            scanChar();
            return T_STAR_EQ;
        }
        return T_STAR;

    case ')': return T_RPAREN;
    case '(': return T_LPAREN;

    case '&':
        if (_char == QLatin1Char('=')) {
            scanChar();
            return T_AND_EQ;
        } else if (_char == QLatin1Char('&')) {
            scanChar();
            return T_AND_AND;
        }
        return T_AND;

    case '%':
        if (_char == QLatin1Char('=')) {
            scanChar();
            return T_REMAINDER_EQ;
        }
        return T_REMAINDER;

    case '!':
        if (_char == QLatin1Char('=')) {
            scanChar();
            if (_char == QLatin1Char('=')) {
                scanChar();
                return T_NOT_EQ_EQ;
            }
            return T_NOT_EQ;
        }
        return T_NOT;

    case '\'':
    case '"': {
        const QChar quote = ch;
        _tokenText.clear();
        _validTokenText = true;

        bool multilineStringLiteral = false;

        while (! _char.isNull()) {
            if (_char == QLatin1Char('\n')) {
                multilineStringLiteral = true;
                _tokenText += _char;
                scanChar();
            } else if (_char == quote) {
                scanChar();

                if (_engine)
                    _tokenSpell = _engine->newStringRef(_tokenText);

                return multilineStringLiteral ? T_MULTILINE_STRING_LITERAL : T_STRING_LITERAL;
            } else if (_char == QLatin1Char('\\')) {
                scanChar();

                QChar u;
                bool ok = false;

                switch (_char.unicode()) {
                // unicode escape sequence
                case 'u':
                    u = decodeUnicodeEscapeCharacter(&ok);
                    if (! ok)
                        u = _char;
                    break;

                // hex escape sequence
                case 'x':
                case 'X':
                    if (isHexDigit(_codePtr[0]) && isHexDigit(_codePtr[1])) {
                        scanChar();

                        const QChar c1 = _char;
                        scanChar();

                        const QChar c2 = _char;
                        scanChar();

                        u = convertHex(c1, c2);
                    } else {
                        u = _char;
                    }
                    break;

                // single character escape sequence
                case '\\': u = QLatin1Char('\''); scanChar(); break;
                case '\'': u = QLatin1Char('\''); scanChar(); break;
                case '\"': u = QLatin1Char('\"'); scanChar(); break;
                case 'b':  u = QLatin1Char('\b'); scanChar(); break;
                case 'f':  u = QLatin1Char('\f'); scanChar(); break;
                case 'n':  u = QLatin1Char('\n'); scanChar(); break;
                case 'r':  u = QLatin1Char('\r'); scanChar(); break;
                case 't':  u = QLatin1Char('\t'); scanChar(); break;
                case 'v':  u = QLatin1Char('\v'); scanChar(); break;

                case '0':
                    if (! _codePtr[1].isDigit()) {
                        scanChar();
                        u = QLatin1Char('\0');
                    } else {
                        // ### parse deprecated octal escape sequence ?
                        u = _char;
                    }
                    break;

                case '\r':
                    while (_char == QLatin1Char('\r'))
                        scanChar();

                    if (_char == '\n') {
                        u = _char;
                        scanChar();
                    } else {
                        u = QLatin1Char('\n');
                    }

                    break;

                case '\n':
                    u = _char;
                    scanChar();
                    break;

                default:
                    // non escape character
                    u = _char;
                    scanChar();
                }

                _tokenText += u;
            } else {
                _tokenText += _char;
                scanChar();
            }
        }

        _errorCode = UnclosedStringLiteral;
        _errorMessage = QCoreApplication::translate("QDeclarativeParser", "Unclosed string at end of line");
        return T_ERROR;
    }

    default:
        if (ch.isLetter() || ch == QLatin1Char('$') || ch == QLatin1Char('_') || (ch == QLatin1Char('\\') && _char == QLatin1Char('u'))) {
            bool identifierWithEscapeChars = false;
            if (ch == QLatin1Char('\\')) {
                identifierWithEscapeChars = true;
                _tokenText.clear();
                bool ok = false;
                _tokenText += decodeUnicodeEscapeCharacter(&ok);
                _validTokenText = true;
                if (! ok) {
                    _errorCode = IllegalUnicodeEscapeSequence;
                    _errorMessage = QCoreApplication::translate("QDeclarativeParser", "Illegal unicode escape sequence");
                    return T_ERROR;
                }
            }
            while (true) {
                if (_char.isLetterOrNumber() || _char == QLatin1Char('$') || _char == QLatin1Char('_')) {
                    if (identifierWithEscapeChars)
                        _tokenText += _char;

                    scanChar();
                } else if (_char == QLatin1Char('\\') && _codePtr[0] == QLatin1Char('u')) {
                    if (! identifierWithEscapeChars) {
                        identifierWithEscapeChars = true;
                        _tokenText = QString(_tokenStartPtr, _codePtr - _tokenStartPtr - 1);
                        _validTokenText = true;
                    }

                    scanChar(); // skip '\\'
                    bool ok = false;
                    _tokenText += decodeUnicodeEscapeCharacter(&ok);
                    if (! ok) {
                        _errorCode = IllegalUnicodeEscapeSequence;
                        _errorMessage = QCoreApplication::translate("QDeclarativeParser", "Illegal unicode escape sequence");
                        return T_ERROR;
                    }
                } else {
                    _tokenLength = _codePtr - _tokenStartPtr - 1;

                    int kind = T_IDENTIFIER;

                    if (! identifierWithEscapeChars)
                        kind = classify(_tokenStartPtr, _tokenLength);

                    if (_engine) {
                        if (kind == T_IDENTIFIER && identifierWithEscapeChars)
                            _tokenSpell = _engine->newStringRef(_tokenText);
                        else
                            _tokenSpell = _engine->midRef(_tokenStartPtr - _code.unicode(), _tokenLength);
                    }

                    return kind;
                }
            }
        } else if (ch.isDigit()) {
            QByteArray chars;
            chars.reserve(32);
            chars += ch.unicode();

            while (_char.isLetterOrNumber() || _char == QLatin1Char('.')) {
                if (_char == QLatin1Char('e') || _char == QLatin1Char('E')) {
                    chars += _char.unicode();
                    scanChar(); // skip e

                    if (_char == QLatin1Char('+') || _char == QLatin1Char('-')) {
                        chars += _char.unicode();
                        scanChar(); // skip +/-
                    }
                } else {
                    chars += _char.unicode();
                    scanChar();
                }
            }
            const char *begin = chars.constData();
            const char *end = 0;
            bool ok = false;

            _tokenValue = qstrtod(begin, &end, &ok);

            if (! ok || end != chars.end()) {
                _errorCode = IllegalExponentIndicator;
                _errorMessage = QCoreApplication::translate("QDeclarativeParser", "Illegal syntax for exponential number");
                return T_ERROR;
            }

            return T_NUMERIC_LITERAL;
        }

        break;
    }

    return T_ERROR;
}

bool Lexer::scanRegExp(RegExpBodyPrefix prefix)
{
    _tokenText.clear();
    _validTokenText = true;
    _patternFlags = 0;

    if (prefix == EqualPrefix)
        _tokenText += QLatin1Char('=');

    while (true) {
        switch (_char.unicode()) {
        case 0: // eof
        case '\n': case '\r': // line terminator
            _errorMessage = QCoreApplication::translate("QDeclarativeParser", "Unterminated regular expression literal");
            return false;

        case '/':
            scanChar();

            // scan the flags
            _patternFlags = 0;
            while (isIdentLetter(_char)) {
                int flag = flagFromChar(_char);
                if (flag == 0) {
                    _errorMessage = QCoreApplication::translate("QDeclarativeParser", "Invalid regular expression flag '%0'")
                             .arg(QChar(_char));
                    return false;
                }
                _patternFlags |= flag;
                scanChar();
            }
            return true;

        case '\\':
            // regular expression backslash sequence
            _tokenText += _char;
            scanChar();

            if (_char.isNull() || isLineTerminator()) {
                _errorMessage = QCoreApplication::translate("QDeclarativeParser", "Unterminated regular expression backslash sequence");
                return false;
            }

            _tokenText += _char;
            scanChar();
            break;

        case '[':
            // regular expression class
            _tokenText += _char;
            scanChar();

            while (! _char.isNull() && ! isLineTerminator()) {
                if (_char == QLatin1Char(']'))
                    break;
                else if (_char == QLatin1Char('\\')) {
                    // regular expression backslash sequence
                    _tokenText += _char;
                    scanChar();

                    if (_char.isNull() || isLineTerminator()) {
                        _errorMessage = QCoreApplication::translate("QDeclarativeParser", "Unterminated regular expression backslash sequence");
                        return false;
                    }

                    _tokenText += _char;
                    scanChar();
                } else {
                    _tokenText += _char;
                    scanChar();
                }
            }

            if (_char != QLatin1Char(']')) {
                _errorMessage = QCoreApplication::translate("QDeclarativeParser", "Unterminated regular expression class");
                return false;
            }

            _tokenText += _char;
            scanChar(); // skip ]
            break;

        default:
            _tokenText += _char;
            scanChar();
        } // switch
    } // while

    return false;
}

bool Lexer::isLineTerminator() const
{
    return (_char == QLatin1Char('\n') || _char == QLatin1Char('\r'));
}

bool Lexer::isIdentLetter(QChar ch)
{
    // ASCII-biased, since all reserved words are ASCII, aand hence the
    // bulk of content to be parsed.
    if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
            || (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
            || ch == QLatin1Char('$')
            || ch == QLatin1Char('_'))
        return true;
    if (ch.unicode() < 128)
        return false;
    return ch.isLetterOrNumber();
}

bool Lexer::isDecimalDigit(ushort c)
{
    return (c >= '0' && c <= '9');
}

bool Lexer::isHexDigit(QChar c)
{
    return ((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
            || (c >= QLatin1Char('A') && c <= QLatin1Char('F')));
}

bool Lexer::isOctalDigit(ushort c)
{
    return (c >= '0' && c <= '7');
}

int Lexer::tokenOffset() const
{
    return _tokenStartPtr - _code.unicode();
}

int Lexer::tokenLength() const
{
    return _tokenLength;
}

int Lexer::tokenStartLine() const
{
    return _tokenLine;
}

int Lexer::tokenStartColumn() const
{
    return _tokenStartPtr - _tokenLinePtr + 1;
}

int Lexer::tokenEndLine() const
{
    return _currentLineNumber;
}

int Lexer::tokenEndColumn() const
{
    return _codePtr - _lastLinePtr;
}

QStringRef Lexer::tokenSpell() const
{
    return _tokenSpell;
}

double Lexer::tokenValue() const
{
    return _tokenValue;
}

QString Lexer::tokenText() const
{
    if (_validTokenText)
        return _tokenText;

    return QString(_tokenStartPtr, _tokenLength);
}

Lexer::Error Lexer::errorCode() const
{
    return _errorCode;
}

QString Lexer::errorMessage() const
{
    return _errorMessage;
}

void Lexer::syncProhibitAutomaticSemicolon()
{
    if (_parenthesesState == BalancedParentheses) {
        // we have seen something like "if (foo)", which means we should
        // never insert an automatic semicolon at this point, since it would
        // then be expanded into an empty statement (ECMA-262 7.9.1)
        _prohibitAutomaticSemicolon = true;
        _parenthesesState = IgnoreParentheses;
    } else {
        _prohibitAutomaticSemicolon = false;
    }
}

bool Lexer::prevTerminator() const
{
    return _terminator;
}

#include "qdeclarativejskeywords_p.h"
