/*
 * sed — stream editor (pragmatic POSIX subset for LoricaOS).
 *
 * Covers what build/configure scripts actually use: substitution
 * (s/re/repl/ with g, p, Nth, i flags, & and \1..\9 backrefs, any
 * delimiter), the p/d/q/= commands, and addresses (line number, $,
 * /regex/, and addr1,addr2 ranges). Flags: -n -e -f -E/-r -i[suffix] -s.
 *
 * Real BRE/ERE via musl regcomp (same as grep). ponytail ceiling: no hold
 * space, no branches (b/t/:), no a/i/c/y — those are rare in scripts and
 * would roughly triple this file; add them behind their command letters if
 * a real script ever needs them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <unistd.h>

#define MAXCMD 256

enum { A_NONE, A_LINE, A_LAST, A_RE };

typedef struct {
    int   type;        /* A_NONE / A_LINE / A_LAST / A_RE */
    long  line;        /* A_LINE */
    regex_t re;        /* A_RE */
    int    has_re;
} addr_t;

typedef struct {
    char   cmd;        /* 's' 'p' 'd' 'q' '=' */
    addr_t a1, a2;     /* a2.type==A_NONE → single address */
    int    naddr;      /* 0, 1, or 2 */
    int    active;     /* range currently open */
    /* s command */
    regex_t s_re;
    char  *s_repl;
    int    s_global;
    int    s_print;
    int    s_nth;      /* replace Nth occurrence (0 = default first) */
} cmd_t;

static cmd_t cmds[MAXCMD];
static int   ncmd;
static int   opt_n;        /* -n suppress auto-print */
static int   opt_E;        /* -E/-r extended regex */
static int   opt_sep;      /* -s separate files (reset line numbers) */
static int   opt_inplace;  /* -i */
static char *inplace_suf;  /* -i suffix (may be "") */
static int   quit;         /* q hit */

static _Noreturn void die(const char *m) { fprintf(stderr, "sed: %s\n", m); exit(1); }

/* Compile a regex, using the previous one if empty (POSIX empty-re reuse). */
static regex_t *last_re;
static void compile_re(regex_t *re, const char *pat, int icase) {
    int fl = (opt_E ? REG_EXTENDED : 0) | (icase ? REG_ICASE : 0);
    if (regcomp(re, pat, fl) != 0) die("invalid regex");
    last_re = re;
}

/* Parse one address at *p; advance *p past it. Returns naddr contribution. */
static int parse_addr(const char **pp, addr_t *a) {
    const char *p = *pp;
    a->type = A_NONE; a->has_re = 0;
    if (*p == '$') { a->type = A_LAST; p++; }
    else if (isdigit((unsigned char)*p)) {
        a->type = A_LINE; a->line = strtol(p, (char **)&p, 10);
    } else if (*p == '/' || (*p == '\\' && p[1])) {
        char delim = '/';
        if (*p == '\\') { p++; delim = *p; }
        p++;
        char buf[1024]; int n = 0;
        while (*p && *p != delim && n < (int)sizeof buf - 1) {
            if (*p == '\\' && p[1] == delim) { buf[n++] = delim; p += 2; continue; }
            buf[n++] = *p++;
        }
        buf[n] = 0;
        if (*p == delim) p++;
        a->type = A_RE; a->has_re = 1;
        compile_re(&a->re, buf, 0);
    } else { *pp = p; return 0; }
    *pp = p;
    return 1;
}

/* Unescape a replacement backslash sequence's literal chars (\n \t \\). */
static void parse_s(const char **pp, cmd_t *c) {
    const char *p = *pp;
    p++;                    /* skip the 's' command letter */
    char delim = *p++;
    char pat[1024]; int pn = 0;
    while (*p && *p != delim) {
        if (*p == '\\' && p[1] == delim) { pat[pn++] = delim; p += 2; continue; }
        if (*p == '\\' && p[1]) { pat[pn++] = *p++; pat[pn++] = *p++; continue; }
        pat[pn++] = *p++;
        if (pn >= (int)sizeof pat - 2) die("regex too long");
    }
    if (*p != delim) die("unterminated s///");
    pat[pn] = 0; p++;

    char rep[2048]; int rn = 0;
    while (*p && *p != delim) {
        if (*p == '\\' && p[1] == delim) { rep[rn++] = delim; p += 2; continue; }
        rep[rn++] = *p++;
        if (rn >= (int)sizeof rep - 2) die("replacement too long");
    }
    if (*p != delim) die("unterminated s///");
    rep[rn] = 0; p++;

    /* flags */
    int icase = 0;
    c->s_global = 0; c->s_print = 0; c->s_nth = 0;
    while (*p && !strchr(";\n", *p) && *p != '}') {
        if (*p == 'g') c->s_global = 1;
        else if (*p == 'p') c->s_print = 1;
        else if (*p == 'i' || *p == 'I') icase = 1;
        else if (isdigit((unsigned char)*p)) c->s_nth = strtol(p, (char **)&p, 10), p--;
        else if (*p == ' ' || *p == '\t') { /* skip */ }
        else break;
        p++;
    }
    compile_re(&c->s_re, pat, icase);
    c->s_repl = strdup(rep);
    *pp = p;
}

static void parse_script(const char *s) {
    const char *p = s;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ';') p++;
        if (!*p) break;
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (ncmd >= MAXCMD) die("too many commands");
        cmd_t *c = &cmds[ncmd];
        memset(c, 0, sizeof *c);
        c->naddr = 0;
        if (parse_addr(&p, &c->a1)) {
            c->naddr = 1;
            if (*p == ',') { p++; parse_addr(&p, &c->a2); c->naddr = 2; }
        }
        while (*p == ' ' || *p == '\t') p++;
        c->cmd = *p;
        switch (*p) {
        case 's': parse_s(&p, c); break;
        case 'p': case 'd': case 'q': case '=': p++; break;
        case '{': case '}': die("command groups unsupported");
        default: die("unsupported command");
        }
        ncmd++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ';' || *p == '\n') p++;
    }
}

/* Does a single address match the current line? */
static int addr1_match(addr_t *a, const char *line, long lno, int last) {
    switch (a->type) {
    case A_LINE: return lno == a->line;
    case A_LAST: return last;
    case A_RE:   return regexec(&a->re, line, 0, NULL, 0) == 0;
    default:     return 0;
    }
}

/* Whole-command selection with range state. */
static int selected(cmd_t *c, const char *line, long lno, int last) {
    if (c->naddr == 0) return 1;
    if (c->naddr == 1) return addr1_match(&c->a1, line, lno, last);
    /* 2-address range */
    if (!c->active) {
        if (addr1_match(&c->a1, line, lno, last)) {
            c->active = 1;
            /* a numeric end already passed → single line */
            if (c->a2.type == A_LINE && c->a2.line <= lno) c->active = 0;
            return 1;
        }
        return 0;
    }
    /* range open: this line is included; close if a2 matches */
    if (c->a2.type == A_LINE) { if (lno >= c->a2.line) c->active = 0; }
    else if (addr1_match(&c->a2, line, lno, last)) c->active = 0;
    return 1;
}

/* Build the substitution result for one match into out. */
static void emit_repl(const char *repl, const char *src, regmatch_t *m,
                      char **out, size_t *olen, size_t *ocap) {
    for (const char *r = repl; *r; r++) {
        char ch;
        if (*r == '&') {
            int len = m[0].rm_eo - m[0].rm_so;
            for (int i = 0; i < len; i++) {
                ch = src[m[0].rm_so + i];
                if (*olen + 1 >= *ocap) *out = realloc(*out, *ocap *= 2);
                (*out)[(*olen)++] = ch;
            }
            continue;
        }
        if (*r == '\\' && r[1]) {
            r++;
            if (*r >= '1' && *r <= '9') {
                int g = *r - '0';
                int len = m[g].rm_eo - m[g].rm_so;
                if (m[g].rm_so < 0) continue;
                for (int i = 0; i < len; i++) {
                    ch = src[m[g].rm_so + i];
                    if (*olen + 1 >= *ocap) *out = realloc(*out, *ocap *= 2);
                    (*out)[(*olen)++] = ch;
                }
                continue;
            }
            ch = (*r == 'n') ? '\n' : (*r == 't') ? '\t' : *r;
        } else ch = *r;
        if (*olen + 1 >= *ocap) *out = realloc(*out, *ocap *= 2);
        (*out)[(*olen)++] = ch;
    }
}

/* Apply s/// to line (modifies *line, may realloc). Returns 1 if substituted. */
static int do_subst(cmd_t *c, char **line) {
    const char *src = *line;
    long len = (long)strlen(src);
    regmatch_t m[10];
    size_t ocap = (size_t)len * 2 + 32, olen = 0;
    char *out = malloc(ocap);
    long off = 0, count = 0, last_end = -1;
    int did = 0, notbol = 0, stop = 0;
#define PUSH(ch) do { if (olen + 1 >= ocap) out = realloc(out, ocap *= 2); out[olen++] = (ch); } while (0)

    while (!stop && off <= len && regexec(&c->s_re, src + off, 10, m, notbol) == 0) {
        long ms = off + m[0].rm_so, me = off + m[0].rm_eo;
        int empty = (me == ms);
        for (long i = off; i < ms; i++) PUSH(src[i]);   /* gap before match */

        /* An empty match butted against the previous match is not a real
         * occurrence (GNU rule): step one char so we don't double-replace. */
        if (empty && ms == last_end) {
            if (ms < len) PUSH(src[ms]);
            off = ms + 1; notbol = REG_NOTBOL; continue;
        }
        count++;
        int do_this = c->s_nth ? (c->s_global ? count >= c->s_nth : count == c->s_nth)
                               : (c->s_global ? 1 : count == 1);
        if (do_this) {
            regmatch_t gm[10];
            for (int i = 0; i < 10; i++) {
                gm[i].rm_so = m[i].rm_so < 0 ? -1 : m[i].rm_so + off;
                gm[i].rm_eo = m[i].rm_eo < 0 ? -1 : m[i].rm_eo + off;
            }
            emit_repl(c->s_repl, src, gm, &out, &olen, &ocap);
            did = 1;
            if (!c->s_global) stop = 1;      /* single replacement done */
        } else {
            for (long i = ms; i < me; i++) PUSH(src[i]);
        }
        last_end = me;
        if (empty) { if (me < len && !stop) PUSH(src[me]); off = me + 1; }
        else off = me;
        notbol = REG_NOTBOL;
    }
    for (long i = off; i < len; i++) PUSH(src[i]);       /* tail */
    PUSH('\0'); olen--;
#undef PUSH
    if (did) { free(*line); *line = out; } else free(out);
    return did;
}

static void process(FILE *in, FILE *out, long *lno, int reset) {
    if (reset) { *lno = 0; for (int i = 0; i < ncmd; i++) cmds[i].active = 0; }
    char *cur = NULL, *nxt = NULL; size_t cc = 0, nc = 0;
    ssize_t clen = getline(&cur, &cc, in);
    while (clen != -1 && !quit) {
        /* strip trailing newline (remember if it was there) */
        int had_nl = (clen > 0 && cur[clen - 1] == '\n');
        if (had_nl) cur[clen - 1] = 0;
        ssize_t nlen = getline(&nxt, &nc, in);
        int last = (nlen == -1);
        (*lno)++;

        int deleted = 0;
        for (int i = 0; i < ncmd; i++) {
            cmd_t *c = &cmds[i];
            if (!selected(c, cur, *lno, last)) continue;
            switch (c->cmd) {
            case 's': if (do_subst(c, &cur) && c->s_print) { fputs(cur, out); fputc('\n', out); } break;
            case 'p': fputs(cur, out); fputc('\n', out); break;
            case 'd': deleted = 1; break;
            case 'q': quit = 1; break;
            case '=': fprintf(out, "%ld\n", *lno); break;
            }
            if (deleted || quit) break;
        }
        if (!opt_n && !deleted) { fputs(cur, out); if (had_nl || !last) fputc('\n', out); }

        /* rotate lookahead into current */
        char *t = cur; cur = nxt; nxt = t;
        size_t ts = cc; cc = nc; nc = ts;
        clen = nlen;
    }
    free(cur); free(nxt);
}

static void run_file(const char *path, long *lno) {
    if (opt_inplace && path) {
        FILE *in = fopen(path, "r");
        if (!in) { fprintf(stderr, "sed: %s: cannot open\n", path); exit(1); }
        char tmp[1200]; snprintf(tmp, sizeof tmp, "%s.sed_tmp", path);
        FILE *out = fopen(tmp, "w");
        if (!out) { fprintf(stderr, "sed: %s: cannot write\n", tmp); exit(1); }
        process(in, out, lno, 1);
        fclose(in); fclose(out);
        if (inplace_suf && inplace_suf[0]) {
            char bak[1300]; snprintf(bak, sizeof bak, "%s%s", path, inplace_suf);
            rename(path, bak);
        }
        rename(tmp, path);
    } else {
        FILE *in = path ? fopen(path, "r") : stdin;
        if (!in) { fprintf(stderr, "sed: %s: cannot open\n", path); exit(1); }
        process(in, stdout, lno, opt_sep);
        if (path) fclose(in);
    }
}

int main(int argc, char **argv) {
    char script_buf[8192]; script_buf[0] = 0;
    int have_script = 0;
    int i = 1;

    for (; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '-' || a[1] == 0) break;
        if (!strcmp(a, "--")) { i++; break; }
        for (int j = 1; a[j]; j++) {
            switch (a[j]) {
            case 'n': opt_n = 1; break;
            case 'E': case 'r': opt_E = 1; break;
            case 's': opt_sep = 1; break;
            case 'i':
                opt_inplace = 1;
                inplace_suf = strdup(a + j + 1);  /* -iSUFFIX (GNU, no space) */
                j = strlen(a) - 1;
                break;
            case 'e': {
                char *v = a[j + 1] ? a + j + 1 : argv[++i];
                if (!v) die("-e needs an argument");
                if (have_script) strncat(script_buf, "\n", sizeof script_buf - strlen(script_buf) - 1);
                strncat(script_buf, v, sizeof script_buf - strlen(script_buf) - 1);
                have_script = 1; j = strlen(a) - 1;
                break;
            }
            case 'f': {
                char *fn = a[j + 1] ? a + j + 1 : argv[++i];
                FILE *sf = fopen(fn, "r");
                if (!sf) die("cannot open script file");
                size_t off = strlen(script_buf);
                if (have_script && off < sizeof script_buf - 1) script_buf[off++] = '\n';
                off += fread(script_buf + off, 1, sizeof script_buf - off - 1, sf);
                script_buf[off] = 0; fclose(sf); have_script = 1; j = strlen(a) - 1;
                break;
            }
            default: die("unknown option");
            }
        }
    }

    if (!have_script) {
        if (i >= argc) die("no script");
        strncat(script_buf, argv[i++], sizeof script_buf - 1);
    }
    parse_script(script_buf);

    long lno = 0;
    if (i >= argc) run_file(NULL, &lno);
    else for (; i < argc && !quit; i++) run_file(argv[i], &lno);
    return 0;
}
