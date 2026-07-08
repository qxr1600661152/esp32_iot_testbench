#include "epd_formula.h"

static void replaceAll(String &s, const char *from, const char *to)
{
    String out;
    const size_t flen = strlen(from);
    for (size_t i = 0; i < (size_t)s.length();) {
        if (s.substring(i, i + flen) == from) {
            out += to;
            i += flen;
        } else {
            out += s[i++];
        }
    }
    s = out;
}

static void stripDollarMath(String &s)
{
    replaceAll(s, "$$", "");
    replaceAll(s, "$", "");
    replaceAll(s, "\\(", "");
    replaceAll(s, "\\)", "");
    replaceAll(s, "\\[", "");
    replaceAll(s, "\\]", "");
}

void epdExpandFormulas(String &text)
{
    stripDollarMath(text);
    replaceAll(text, "\\int", "∫");
    replaceAll(text, "\\sum", "Σ");
    replaceAll(text, "\\infty", "∞");
    replaceAll(text, "\\pi", "π");
    replaceAll(text, "\\alpha", "α");
    replaceAll(text, "\\beta", "β");
    replaceAll(text, "\\gamma", "γ");
    replaceAll(text, "\\Delta", "Δ");
    replaceAll(text, "\\theta", "θ");
    replaceAll(text, "\\lambda", "λ");
    replaceAll(text, "\\mu", "μ");
    replaceAll(text, "\\sigma", "σ");
    replaceAll(text, "\\omega", "ω");
    replaceAll(text, "\\cdot", "·");
    replaceAll(text, "\\times", "×");
    replaceAll(text, "\\leq", "≤");
    replaceAll(text, "\\geq", "≥");
    replaceAll(text, "\\neq", "≠");
    replaceAll(text, "\\approx", "≈");
    replaceAll(text, "\\partial", "∂");
    replaceAll(text, "\\sqrt", "√");
    replaceAll(text, "^{2}", "²");
    replaceAll(text, "^2", "²");
    replaceAll(text, "^{3}", "³");
    replaceAll(text, "^3", "³");
    replaceAll(text, "\\frac{", "(");
    replaceAll(text, "}{", "/");

    text.replace("{", "");
    text.replace("}", "");
}
