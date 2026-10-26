// Hands-on 2: Laboratorio interactivo de analizadores léxicos (estilo Lex/Flex)
// Expresión regular -> AFN (Thompson) -> simulación paralela de todas las reglas -> máxima coincidencia.
// CLI:  g++ -std=c++17 -O2 lex_lab.cpp -o lex_lab && ./lex_lab programa.c
// Web:  emcc lex_lab.cpp -std=c++17 -O2 -sALLOW_MEMORY_GROWTH=1 -o lex_lab.js
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <bitset>
#include <map>
#include <algorithm>
#include <cstdio>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
using namespace std;

// ---------- 0. Especificación Lex por defecto (49 reglas, subconjunto de C) ----------
static const char* SPEC_DEFECTO = R"LEX(%{
#include <stdio.h>
/* Acciones del laboratorio (se simulan; no se ejecuta C arbitrario):
   TOKEN("N")   imprime el token N junto con yytext y lo agrega al resultado
   OMITIR("N")  descarta el lexema (espacios y comentarios)
   ERROR("N")   informa un caracter no reconocido y lo consume */
#define TOKEN(n)  printf("%-18s %s\n", n, yytext)
#define OMITIR(n) /* no imprime nada */
#define ERROR(n)  fprintf(stderr, "error: '%s'\n", yytext)
%}
DIGITO       [0-9]
ENTERO       {DIGITO}+
NOMBRE       [A-Za-z_][A-Za-z0-9_]*
DECIMAL      {ENTERO}[.]{ENTERO}
EXPONENTE    [eE][+-]?{ENTERO}
CIENTIFICO   ({ENTERO}|{DECIMAL}){EXPONENTE}
NORMAL       [^"\\\r\n]
ESCAPE       \\["\\ntr]
CADENA       \"({NORMAL}|{ESCAPE})*\"

%%
#[ \t]*include   { TOKEN("PP_INCLUDE"); }
#[ \t]*define    { TOKEN("PP_DEFINE"); }
"int"            { TOKEN("KW_INT"); }
"float"          { TOKEN("KW_FLOAT"); }
"double"         { TOKEN("KW_DOUBLE"); }
"char"           { TOKEN("KW_CHAR"); }
"void"           { TOKEN("KW_VOID"); }
"return"         { TOKEN("KW_RETURN"); }
"if"             { TOKEN("KW_IF"); }
"else"           { TOKEN("KW_ELSE"); }
"for"            { TOKEN("KW_FOR"); }
"while"          { TOKEN("KW_WHILE"); }
"do"             { TOKEN("KW_DO"); }
"break"          { TOKEN("KW_BREAK"); }
"continue"       { TOKEN("KW_CONTINUE"); }
{NOMBRE}         { TOKEN("IDENTIFICADOR"); }
{CIENTIFICO}     { TOKEN("NUM_CIENTIFICO"); }
{DECIMAL}        { TOKEN("NUM_DECIMAL"); }
{ENTERO}         { TOKEN("NUM_ENTERO"); }
{CADENA}         { TOKEN("CADENA"); }
"/*"([^*]|"*"+[^*/])*"*"+"/"  { OMITIR("COMENTARIO_BLOQUE"); }
"//"[^\n]*       { OMITIR("COMENTARIO_LINEA"); }
"++"             { TOKEN("OP_INCREMENTO"); }
"--"             { TOKEN("OP_DECREMENTO"); }
"->"             { TOKEN("OP_FLECHA"); }
"<<"             { TOKEN("OP_DESPLAZ_IZQ"); }
">>"             { TOKEN("OP_DESPLAZ_DER"); }
"<="             { TOKEN("OP_MENOR_IGUAL"); }
">="             { TOKEN("OP_MAYOR_IGUAL"); }
"=="             { TOKEN("OP_IGUAL"); }
"!="             { TOKEN("OP_DIFERENTE"); }
"&&"             { TOKEN("OP_Y"); }
"||"             { TOKEN("OP_O"); }
"+="             { TOKEN("OP_SUMA_ASIG"); }
"-="             { TOKEN("OP_RESTA_ASIG"); }
"*="             { TOKEN("OP_MULT_ASIG"); }
"/="             { TOKEN("OP_DIV_ASIG"); }
"%="             { TOKEN("OP_MOD_ASIG"); }
"="              { TOKEN("OP_ASIGNACION"); }
[-+*/%]          { TOKEN("OP_ARITMETICO"); }
[<>]             { TOKEN("OP_RELACIONAL"); }
"!"              { TOKEN("OP_NEGACION"); }
[&|^~]           { TOKEN("OP_BIT"); }
[;,.:?]          { TOKEN("PUNTUACION"); }
[()]             { TOKEN("PAREN"); }
[{}]             { TOKEN("LLAVE"); }
[\[\]]           { TOKEN("CORCHETE"); }
[ \t\r\n]+       { OMITIR("ESPACIO"); }
.                { ERROR("ERROR"); }

%%
int yywrap(void) { return 1; }
int main(void) {
    yyin = stdin;
    yylex();
    return 0;
}
)LEX";

// ---------- 1. Expresiones regulares -> AFN de Thompson ----------
// Nodo con set<0: transición epsilon hacia a (y b si existe). Nodo con set>=0: consume un símbolo de sets[set] y va a a.
struct Nodo { int a = -1, b = -1, set = -1; };
struct Frag { int s = -1, f = -1; };
struct Afn {
  vector<Nodo> n; vector<bitset<256>> sets; int ini = -1, fin = -1;
  int nuevo() { n.push_back(Nodo()); return (int)n.size() - 1; }
  int conj(const bitset<256>& b) { sets.push_back(b); return (int)sets.size() - 1; }
};

// Sin excepciones (Emscripten las desactiva por defecto): los errores se acumulan en 'err'.
struct ParserRx {
  const string& p; size_t i = 0; Afn& A; string err;
  ParserRx(const string& s, Afn& a) : p(s), A(a) {}
  bool fin() const { return i >= p.size(); }
  Frag mal(const string& m) { if (err.empty()) err = m; int s = A.nuevo(); Frag f; f.s = s; f.f = s; return f; }
  Frag vacio() { int s = A.nuevo(); Frag f; f.s = s; f.f = s; return f; }
  Frag conjunto(const bitset<256>& b) {
    int s = A.nuevo(), f = A.nuevo(); A.n[s].set = A.conj(b); A.n[s].a = f; Frag r; r.s = s; r.f = f; return r;
  }
  Frag simbolo(int c) { bitset<256> b; b.set((unsigned char)c); return conjunto(b); }
  Frag cat(Frag a, Frag b) { A.n[a.f].a = b.s; Frag r; r.s = a.s; r.f = b.f; return r; }
  Frag unir(Frag a, Frag b) {
    int s = A.nuevo(), f = A.nuevo(); A.n[s].a = a.s; A.n[s].b = b.s; A.n[a.f].a = f; A.n[b.f].a = f;
    Frag r; r.s = s; r.f = f; return r;
  }
  Frag estrella(Frag a) {
    int s = A.nuevo(), f = A.nuevo(); A.n[s].a = a.s; A.n[s].b = f; A.n[a.f].a = a.s; A.n[a.f].b = f;
    Frag r; r.s = s; r.f = f; return r;
  }
  Frag mas(Frag a) { int f = A.nuevo(); A.n[a.f].a = a.s; A.n[a.f].b = f; Frag r; r.s = a.s; r.f = f; return r; }
  Frag opcional(Frag a) {
    int s = A.nuevo(), f = A.nuevo(); A.n[s].a = a.s; A.n[s].b = f; A.n[a.f].a = f;
    Frag r; r.s = s; r.f = f; return r;
  }
  int escape() {  // p[i] es el carácter tras la barra invertida
    if (fin()) { if (err.empty()) err = "barra invertida al final del patrón"; return 0; }
    char e = p[i++];
    switch (e) { case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r'; case 'f': return '\f'; case 'v': return '\v'; default: return (unsigned char)e; }
  }
  Frag alt() {
    Frag l = concat();
    while (err.empty() && !fin() && p[i] == '|') { i++; Frag r = concat(); l = unir(l, r); }
    return l;
  }
  Frag concat() {
    Frag r; bool primero = true;
    while (err.empty() && !fin() && p[i] != '|' && p[i] != ')') {
      Frag x = repet(); if (!err.empty()) return r;
      if (primero) { r = x; primero = false; } else r = cat(r, x);
    }
    return primero ? vacio() : r;
  }
  Frag repet() {
    Frag a = atomo();
    while (err.empty() && !fin() && (p[i] == '*' || p[i] == '+' || p[i] == '?')) {
      char c = p[i++]; a = c == '*' ? estrella(a) : c == '+' ? mas(a) : opcional(a);
    }
    return a;
  }
  Frag atomo() {
    char c = p[i];
    if (c == '(') {
      i++; Frag f = alt(); if (!err.empty()) return f;
      if (fin() || p[i] != ')') return mal("falta ')' para cerrar el grupo");
      i++; return f;
    }
    if (c == '[') return clase();
    if (c == '"') {
      i++; Frag r; bool primero = true;
      while (true) {
        if (fin()) return mal("comillas sin cerrar en el patrón");
        char d = p[i++]; if (d == '"') break;
        int v = d == '\\' ? escape() : (unsigned char)d; if (!err.empty()) return r;
        Frag x = simbolo(v); if (primero) { r = x; primero = false; } else r = cat(r, x);
      }
      return primero ? vacio() : r;
    }
    if (c == '.') { i++; bitset<256> b; b.set(); b.reset('\n'); return conjunto(b); }
    if (c == '\\') { i++; int v = escape(); if (!err.empty()) return vacio(); return simbolo(v); }
    if (c == '*' || c == '+' || c == '?') return mal(string("el operador '") + c + "' no tiene operando");
    if (c == ')') return mal("')' sin grupo abierto");
    if (c == '^' || c == '$' || c == '/') return mal(string("'") + c + "' no está soportado (anclas y contexto final)");
    if (c == '{' || c == '}') return mal("llave inesperada: use {NOMBRE} solo para definiciones");
    i++; return simbolo((unsigned char)c);
  }
  int carClase() {
    char c = p[i++]; if (c == '\\') return escape(); return (unsigned char)c;
  }
  Frag clase() {
    i++; bool neg = false; bitset<256> b;
    if (!fin() && p[i] == '^') { neg = true; i++; }
    bool primero = true;
    while (true) {
      if (fin()) return mal("corchete '[' sin cerrar");
      if (p[i] == ']' && !primero) { i++; break; }
      primero = false;
      int lo = carClase(); if (!err.empty()) return vacio();
      if (!fin() && p[i] == '-' && i + 1 < p.size() && p[i + 1] != ']') {
        i++; int hi = carClase(); if (!err.empty()) return vacio();
        if (hi < lo) return mal("rango invertido en una clase de caracteres");
        for (int k = lo; k <= hi; k++) b.set(k);
      } else b.set(lo);
    }
    if (neg) b.flip();
    return conjunto(b);
  }
};

static vector<int> cierreDe(const Afn& A, const vector<int>& ini) {
  vector<char> vis(A.n.size(), 0); vector<int> pila, res;
  for (int x : ini) if (!vis[x]) { vis[x] = 1; pila.push_back(x); }
  while (!pila.empty()) {
    int x = pila.back(); pila.pop_back(); res.push_back(x);
    const Nodo& N = A.n[x];
    if (N.set < 0) {
      if (N.a >= 0 && !vis[N.a]) { vis[N.a] = 1; pila.push_back(N.a); }
      if (N.b >= 0 && !vis[N.b]) { vis[N.b] = 1; pila.push_back(N.b); }
    }
  }
  return res;
}
static vector<int> avanzar(const Afn& A, const vector<int>& S, unsigned char c) {
  vector<int> sig;
  for (int x : S) { const Nodo& N = A.n[x]; if (N.set >= 0 && A.sets[N.set][c]) sig.push_back(N.a); }
  return cierreDe(A, sig);
}
static bool acepta(const Afn& A, const vector<int>& S) { for (int x : S) if (x == A.fin) return true; return false; }

// ---------- 2. Especificación Lex: definiciones, reglas y acciones ----------
struct ReglaLex { int id = 0; string nombre, patron, accion, tipo; Afn afn; };  // tipo: token | omitir | error
struct Espec { vector<ReglaLex> reglas; vector<string> avisos; string texto; };

static string recortar(const string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"); if (a == string::npos) return "";
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
// Fin del patrón: primer espacio fuera de comillas y de clases (como Flex).
static size_t finPatron(const string& l, size_t i, string* abierto = nullptr) {
  bool com = false, cls = false;
  for (; i < l.size(); ++i) {
    char c = l[i];
    if (c == '\\') { ++i; continue; }
    if (com) { if (c == '"') com = false; continue; }
    if (cls) { if (c == ']') cls = false; continue; }
    if (c == '"') com = true; else if (c == '[') cls = true; else if (c == ' ' || c == '\t') break;
  }
  if (abierto) *abierto = com ? "comillas sin cerrar en el patrón" : cls ? "corchete '[' sin cerrar en el patrón" : "";
  return i;
}
static bool expandir(const string& pat, const map<string, string>& defs, int prof, string& out, string& err) {
  if (prof > 8) { err = "definiciones recursivas"; return false; }
  bool com = false, cls = false;
  for (size_t i = 0; i < pat.size(); ++i) {
    char c = pat[i];
    if (c == '\\') { out += c; if (i + 1 < pat.size()) out += pat[++i]; continue; }
    if (com) { out += c; if (c == '"') com = false; continue; }
    if (cls) { out += c; if (c == ']') cls = false; continue; }
    if (c == '"') { com = true; out += c; continue; }
    if (c == '[') { cls = true; out += c; continue; }
    if (c == '{') {
      size_t q = pat.find('}', i);
      if (q == string::npos) { err = "llave '{' sin cerrar"; return false; }
      string nom = pat.substr(i + 1, q - i - 1);
      bool ident = !nom.empty() && !isdigit((unsigned char)nom[0]);
      for (char ch : nom) if (!(isalnum((unsigned char)ch) || ch == '_')) ident = false;
      if (!ident) { err = "las repeticiones {n,m} no están soportadas"; return false; }
      auto it = defs.find(nom);
      if (it == defs.end()) { err = "la definición {" + nom + "} no existe"; return false; }
      string sub; if (!expandir(it->second, defs, prof + 1, sub, err)) return false;
      out += "(" + sub + ")"; i = q; continue;
    }
    out += c;
  }
  return true;
}
static void clasificar(ReglaLex& R, vector<string>& avisos) {
  const string& a = R.accion;
  auto buscar = [&](const string& m) {
    size_t p = a.find(m + "(\""); if (p == string::npos) return false;
    size_t d = p + m.size() + 2, q = a.find('"', d); if (q == string::npos) return false;
    R.nombre = a.substr(d, q - d); return true;
  };
  if (buscar("TOKEN")) R.tipo = "token";
  else if (buscar("OMITIR")) R.tipo = "omitir";
  else if (buscar("ERROR")) R.tipo = "error";
  else {
    R.tipo = "omitir"; R.nombre = "REGLA_" + to_string(R.id);
    avisos.push_back("regla " + to_string(R.id) + ": acción no reconocida (solo TOKEN, OMITIR y ERROR); se trata como omisión");
  }
}
static bool compilarEspec(const string& texto, Espec& E, string& err) {
  E = Espec(); E.texto = texto;
  vector<string> L; { string cur; for (char c : texto) { if (c == '\n') { L.push_back(cur); cur.clear(); } else if (c != '\r') cur += c; } L.push_back(cur); }
  int seccion = 0; bool prologo = false, coment = false; map<string, string> defs;
  for (size_t k = 0; k < L.size(); ++k) {
    const string& l = L[k]; string ctx = "línea " + to_string(k + 1) + ": ";
    if (prologo) { if (recortar(l) == "%}") prologo = false; continue; }
    if (coment) { if (l.find("*/") != string::npos) coment = false; continue; }
    if (recortar(l) == "%%") { if (++seccion >= 2) break; continue; }
    if (seccion == 0 && recortar(l) == "%{") { prologo = true; continue; }
    if (l.empty() || recortar(l).empty() || l[0] == ' ' || l[0] == '\t') continue;
    if (l.compare(0, 2, "/*") == 0) { if (l.find("*/", 2) == string::npos) coment = true; continue; }
    if (seccion == 0) {
      size_t i = 0; while (i < l.size() && (isalnum((unsigned char)l[i]) || l[i] == '_')) ++i;
      if (i == 0 || isdigit((unsigned char)l[0])) { err = ctx + "en la sección de definiciones se esperaba NOMBRE patrón (¿falta el separador %%?)"; return false; }
      string nom = l.substr(0, i); size_t a = l.find_first_not_of(" \t", i);
      if (a == string::npos) { err = ctx + "la definición " + nom + " no tiene patrón"; return false; }
      defs[nom] = l.substr(a, finPatron(l, a) - a);
    } else {
      string abierto; size_t e = finPatron(l, 0, &abierto);
      if (!abierto.empty()) { err = ctx + "regla " + to_string(E.reglas.size() + 1) + ": " + abierto; return false; }
      ReglaLex R; R.id = (int)E.reglas.size() + 1; R.patron = l.substr(0, e);
      string resto = recortar(l.substr(e));
      if (!resto.empty() && resto[0] == '{') {  // acción con llaves; puede continuar en varias líneas
        auto prof = [](const string& s) { int d = 0; bool q = false; for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '\\') { ++i; continue; } if (s[i] == '"') q = !q; else if (!q && s[i] == '{') d++; else if (!q && s[i] == '}') d--; } return d; };
        while (prof(resto) > 0 && k + 1 < L.size()) resto += "\n" + recortar(L[++k]);
        if (prof(resto) > 0) { err = ctx + "acción con '{' sin cerrar"; return false; }
      }
      if (resto.empty()) { err = ctx + "la regla " + to_string(R.id) + " no tiene acción"; return false; }
      R.accion = resto;
      for (unsigned char ch : R.patron) if (ch >= 0x80) { err = ctx + "los patrones deben ser ASCII"; return false; }
      string exp, e2; if (!expandir(R.patron, defs, 0, exp, e2)) { err = ctx + e2; return false; }
      ParserRx P(exp, R.afn); Frag f = P.alt();
      if (P.err.empty() && !P.fin()) P.err = "')' sin grupo abierto";
      if (!P.err.empty()) { err = ctx + "regla " + to_string(R.id) + ": " + P.err; return false; }
      R.afn.ini = f.s; R.afn.fin = f.f;
      if (acepta(R.afn, cierreDe(R.afn, vector<int>(1, R.afn.ini)))) { err = ctx + "la regla " + to_string(R.id) + " acepta la cadena vacía"; return false; }
      clasificar(R, E.avisos); E.reglas.push_back(R);
      if (E.reglas.size() > 120) { err = ctx + "máximo 120 reglas"; return false; }
    }
  }
  if (seccion < 1) { err = "falta el separador %% entre las definiciones y las reglas"; return false; }
  if (E.reglas.empty()) { err = "la especificación no tiene reglas"; return false; }
  return true;
}

// ---------- 3. Análisis léxico: todas las reglas compiten; gana la mayor longitud, luego el menor número ----------
struct Paso { int pos = 0; string ch; bool eof = false; vector<int> vivas, aceptan; int mejor = -1, mejorLong = 0; };
struct Lexema {
  int ini = 0, len = 0, linea = 1, col = 1, regla = -1; string tipo, nombre, texto; vector<Paso> pasos; vector<pair<int,int>> acepta;
};
struct Analisis { string entrada; int simbolos = 0, lineas = 1, tokens = 0, omisiones = 0, errores = 0; vector<Lexema> lex; };

// Cada carácter no ASCII (secuencia UTF-8) es UN símbolo (0xFF); '.' y las clases negadas lo aceptan.
static void decodificar(const string& s, string& sim, vector<size_t>& bo) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = (unsigned char)s[i]; size_t n = 1;
    if (c >= 0xC0) { size_t need = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1, k = 1; while (k <= need && i + k < s.size() && (((unsigned char)s[i + k]) & 0xC0) == 0x80) k++; n = k; }
    bo.push_back(i); sim.push_back(c < 0x80 ? (char)c : (char)0xFF); i += n;
  }
  bo.push_back(s.size());
}

static Analisis analizar(const Espec& E, const string& entrada) {
  Analisis R; R.entrada = entrada;
  string sim; vector<size_t> bo; decodificar(entrada, sim, bo);
  int n = (int)sim.size(), nr = (int)E.reglas.size();
  R.simbolos = n; for (char c : sim) if (c == '\n') R.lineas++;
  vector<vector<int>> ini(nr);
  for (int r = 0; r < nr; ++r) ini[r] = cierreDe(E.reglas[r].afn, vector<int>(1, E.reglas[r].afn.ini));
  int pos = 0, linea = 1, col = 1;
  while (pos < n) {
    vector<vector<int>> st = ini; vector<char> viva(nr, 1); vector<int> ultimo(nr, 0);
    int mejor = -1, mejorLong = 0, k = pos;
    Lexema L; L.ini = pos; L.linea = linea; L.col = col;
    while (true) {
      Paso P; P.pos = k;
      if (k >= n) {
        P.eof = true; for (int r = 0; r < nr; ++r) if (viva[r]) P.vivas.push_back(r + 1);
        P.mejor = mejor + 1; P.mejorLong = mejorLong; L.pasos.push_back(P); break;
      }
      unsigned char c = (unsigned char)sim[k]; P.ch = entrada.substr(bo[k], bo[k + 1] - bo[k]);
      for (int r = 0; r < nr; ++r) {
        if (!viva[r]) continue;
        st[r] = avanzar(E.reglas[r].afn, st[r], c);
        if (st[r].empty()) { viva[r] = 0; continue; }
        P.vivas.push_back(r + 1);
        if (acepta(E.reglas[r].afn, st[r])) { P.aceptan.push_back(r + 1); ultimo[r] = k + 1 - pos; }
      }
      if (!P.aceptan.empty()) { mejor = P.aceptan[0] - 1; mejorLong = k + 1 - pos; }  // más largo; en empate, menor número
      P.mejor = mejor + 1; P.mejorLong = mejorLong; L.pasos.push_back(P); k++;
      if (P.vivas.empty()) break;
    }
    for (int r = 0; r < nr; ++r) if (ultimo[r] > 0) L.acepta.push_back(make_pair(r + 1, ultimo[r]));
    if (mejor >= 0) { L.regla = mejor + 1; L.len = mejorLong; L.tipo = E.reglas[mejor].tipo; L.nombre = E.reglas[mejor].nombre; }
    else { L.regla = 0; L.len = 1; L.tipo = "defecto"; L.nombre = "ECHO"; }  // regla por defecto de Flex: copia el carácter
    L.texto = entrada.substr(bo[pos], bo[pos + L.len] - bo[pos]);
    for (int j = pos; j < pos + L.len; ++j) { if (sim[j] == '\n') { linea++; col = 1; } else col++; }
    if (L.tipo == "token") R.tokens++; else if (L.tipo == "omitir") R.omisiones++; else R.errores++;
    pos += L.len; R.lex.push_back(L);
  }
  return R;
}

// ---------- 4. Salidas: JSON (interfaz web) y texto (consola) ----------
static string esc(const string& s) {
  string o;
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break;
      case '\t': o += "\\t"; break; case '\r': o += "\\r"; break;
      default: if (c < 32) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c;
    }
  }
  return o;
}
static string lista(const vector<int>& v) { string o = "["; for (size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + to_string(v[i]); return o + "]"; }
[[maybe_unused]] static string jsonErr(const string& e) { return "{\"ok\":false,\"error\":\"" + esc(e) + "\"}"; }
[[maybe_unused]] static string jsonEspec(const Espec& E) {
  string o = "{\"ok\":true,\"error\":\"\",\"avisos\":[";
  for (size_t i = 0; i < E.avisos.size(); ++i) o += (i ? ",\"" : "\"") + esc(E.avisos[i]) + "\"";
  o += "],\"texto\":\"" + esc(E.texto) + "\",\"reglas\":[";
  for (size_t i = 0; i < E.reglas.size(); ++i) {
    const ReglaLex& r = E.reglas[i];
    o += string(i ? "," : "") + "{\"id\":" + to_string(r.id) + ",\"nombre\":\"" + esc(r.nombre) + "\",\"patron\":\"" + esc(r.patron) +
         "\",\"accion\":\"" + esc(r.accion) + "\",\"tipo\":\"" + r.tipo + "\"}";
  }
  return o + "]}";
}
static string jsonAnalisis(const Analisis& A) {
  string o = "{\"ok\":true,\"simbolos\":" + to_string(A.simbolos) + ",\"lineas\":" + to_string(A.lineas) + ",\"resumen\":{\"tokens\":" +
             to_string(A.tokens) + ",\"omisiones\":" + to_string(A.omisiones) + ",\"errores\":" + to_string(A.errores) + "},\"lexemas\":[";
  for (size_t i = 0; i < A.lex.size(); ++i) {
    const Lexema& L = A.lex[i];
    o += string(i ? "," : "") + "{\"ini\":" + to_string(L.ini) + ",\"len\":" + to_string(L.len) + ",\"linea\":" + to_string(L.linea) +
         ",\"col\":" + to_string(L.col) + ",\"regla\":" + to_string(L.regla) + ",\"tipo\":\"" + L.tipo + "\",\"nombre\":\"" + esc(L.nombre) +
         "\",\"texto\":\"" + esc(L.texto) + "\",\"acepta\":[";
    for (size_t j = 0; j < L.acepta.size(); ++j) o += string(j ? "," : "") + "[" + to_string(L.acepta[j].first) + "," + to_string(L.acepta[j].second) + "]";
    o += "],\"pasos\":[";
    for (size_t j = 0; j < L.pasos.size(); ++j) {
      const Paso& P = L.pasos[j];
      o += string(j ? "," : "") + "{\"p\":" + to_string(P.pos) + ",\"c\":\"" + esc(P.ch) + "\",\"e\":" + (P.eof ? "1" : "0") + ",\"v\":" + lista(P.vivas) +
           ",\"a\":" + lista(P.aceptan) + ",\"m\":" + to_string(P.mejor) + ",\"ml\":" + to_string(P.mejorLong) + "}";
    }
    o += "]}";
  }
  return o + "]}";
}
static void imprimir(const Espec& E, const Analisis& A, bool traza) {
  cout << "ENTRADA\n" << A.entrada << "\n\nLEXEMAS RECONOCIDOS (línea:col  token  yytext  [regla]  yyleng)\n";
  for (const Lexema& L : A.lex) {
    if (L.tipo == "omitir") continue;
    cout << L.linea << ":" << L.col << "\t" << (L.tipo == "token" ? "" : "[" + L.tipo + "] ") << L.nombre << "\t\"" << esc(L.texto) << "\"\t#" << L.regla << "\t" << L.len << "\n";
  }
  cout << "\nRESUMEN: " << A.tokens << " tokens · " << A.omisiones << " omisiones · " << A.errores << " errores · " << E.reglas.size() << " reglas\n";
  if (traza) {
    cout << "\nTRAZA (por lexema: reglas que sobreviven tras cada carácter)\n";
    for (const Lexema& L : A.lex) {
      cout << "@" << L.linea << ":" << L.col << " ";
      for (const Paso& P : L.pasos) cout << (P.eof ? string("<EOF>") : "'" + esc(P.ch) + "'") << "->" << P.vivas.size() << (P.aceptan.empty() ? "" : "*") << " ";
      cout << "=> " << L.nombre << " (regla " << L.regla << ", " << L.len << ")\n";
    }
  }
}

// ---------- 5. Interfaces ----------
#ifdef __EMSCRIPTEN__
// Interfaz WebAssembly SOLO con enteros (igual que el Hands-on 1): JS envía bytes y lee el JSON byte a byte.
static string g_buf[2], g_out; static int g_canal = 0; static Espec g_esp; static bool g_listo = false;
static Espec& vigente() { if (!g_listo) { string e; compilarEspec(SPEC_DEFECTO, g_esp, e); g_listo = true; } return g_esp; }
extern "C" {
  EMSCRIPTEN_KEEPALIVE void lx_canal(int c) { g_canal = c ? 1 : 0; }           // 0 = programa C, 1 = especificación .l
  EMSCRIPTEN_KEEPALIVE void lx_limpiar() { g_buf[g_canal].clear(); }
  EMSCRIPTEN_KEEPALIVE void lx_byte(int b) { g_buf[g_canal].push_back((char)b); }
  // modo 0: analizar el programa con las reglas vigentes · 1: compilar y activar las reglas del canal 1
  // modo 2: restaurar las reglas originales · 3: devolver el texto .l vigente (sin JSON)
  EMSCRIPTEN_KEEPALIVE int lx_procesar(int modo) {
    if (modo == 0) {
      if (g_buf[0].size() > 60000) g_out = jsonErr("el programa de entrada es demasiado largo (máximo 60000 bytes)");
      else g_out = jsonAnalisis(analizar(vigente(), g_buf[0]));
    } else if (modo == 1) {
      Espec nueva; string e;
      if (compilarEspec(g_buf[1], nueva, e)) { g_esp = nueva; g_listo = true; g_out = jsonEspec(g_esp); } else g_out = jsonErr(e);
    } else if (modo == 2) { string e; compilarEspec(SPEC_DEFECTO, g_esp, e); g_listo = true; g_out = jsonEspec(g_esp); }
    else g_out = vigente().texto;
    return (int)g_out.size();
  }
  EMSCRIPTEN_KEEPALIVE int lx_leer(int i) { return (unsigned char)g_out[i]; }
}
#else
int main(int argc, char** argv) {
  bool js = false, traza = false, spec = false; string archivo, reglas;
  for (int i = 1; i < argc; ++i) {
    string a = argv[i];
    if (a == "--json") js = true; else if (a == "--traza") traza = true; else if (a == "--spec") spec = true;
    else if (a == "--reglas" && i + 1 < argc) reglas = argv[++i]; else archivo = a;
  }
  if (spec) { cout << SPEC_DEFECTO; return 0; }
  string texto = SPEC_DEFECTO;
  if (!reglas.empty()) { ifstream f(reglas); if (!f) { cerr << "no se pudo abrir " << reglas << "\n"; return 1; } stringstream ss; ss << f.rdbuf(); texto = ss.str(); }
  Espec E; string err;
  if (!compilarEspec(texto, E, err)) { cerr << "error en la especificación: " << err << "\n"; return 1; }
  string in;
  if (!archivo.empty()) { ifstream f(archivo); if (!f) { cerr << "no se pudo abrir " << archivo << "\n"; return 1; } stringstream ss; ss << f.rdbuf(); in = ss.str(); }
  else { stringstream ss; ss << cin.rdbuf(); in = ss.str(); }
  Analisis A = analizar(E, in);
  if (js) cout << jsonAnalisis(A) << "\n"; else imprimir(E, A, traza);
}
#endif
