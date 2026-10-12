/* boxes.c - a verse as a tree of boxes (see boxes.h). */
#include "boxes.h"

#include <string.h>

#define MAX_DEPTH 32

/* A box being filled. Word boxes keep the word's own Strong's numbers and
 * morphology apart, to put them under the word at the end. */
typedef struct {
    KsBox *box;
    gboolean layer0;   /* word box: still on the word itself (no break yet) */
    guint word_end;    /* word box: items in the first layer */
    GArray *strong;    /* word box: KsItem */
    GArray *morph;
} Open;

typedef struct {
    Open stack[MAX_DEPTH];
    int depth;
    KsBox *root;
    gboolean any_box;
    int note_seq;
} Builder;

static KsBox *box_new(unsigned flags)
{
    KsBox *b = g_new0(KsBox, 1);

    b->flags = flags;
    b->items = g_array_new(FALSE, TRUE, sizeof(KsItem));
    return b;
}

static void item_clear(KsItem *it)
{
    g_free(it->text);
    g_free(it->extra);
    if (it->box)
        ks_box_free(it->box);
}

void ks_box_free(KsBox *b)
{
    if (!b)
        return;
    for (guint i = 0; i < b->items->len; i++)
        item_clear(&g_array_index(b->items, KsItem, i));
    g_array_free(b->items, TRUE);
    g_free(b);
}

static Open *top(Builder *bd)
{
    return &bd->stack[bd->depth - 1];
}

static void add(GArray *items, KsItemType type)
{
    KsItem it = { 0 };
    KsItem *last = items->len ? &g_array_index(items, KsItem, items->len - 1) : NULL;

    /* No space at the start of a line, nor two in a row. */
    if (type == KS_ITEM_SPACE && (!last || last->type == KS_ITEM_SPACE || last->type == KS_ITEM_BREAK))
        return;
    if (type == KS_ITEM_BREAK && last && last->type == KS_ITEM_SPACE)
        g_array_set_size(items, items->len - 1);
    it.type = type;
    g_array_append_val(items, it);
}

static void add_run(GArray *items, const KsSpan *sp, KsSpanKind kind, const char *text, int len)
{
    KsItem it = { 0 };

    it.type = KS_ITEM_RUN;
    it.kind = kind;
    it.style = sp->style;
    it.color = sp->color;
    it.text = g_strndup(text, len);
    g_array_append_val(items, it);
}

/* Text: words become runs, whitespace a single space item. */
static void add_text(GArray *items, const KsSpan *sp)
{
    const char *t = sp->text, *end = sp->text + sp->len;

    while (t < end) {
        const char *w = t;

        if (*t == ' ' || *t == '\t') {
            add(items, KS_ITEM_SPACE);
            while (t < end && (*t == ' ' || *t == '\t'))
                t++;
            continue;
        }
        while (t < end && *t != ' ' && *t != '\t')
            t++;
        add_run(items, sp, KS_SPAN_TEXT, w, t - w);
    }
}

static void open_box(Builder *bd, unsigned flags)
{
    Open *o;
    KsItem it = { 0 };

    if (bd->depth >= MAX_DEPTH)
        return;
    it.type = KS_ITEM_BOX;
    it.box = box_new(flags);
    g_array_append_val(top(bd)->box->items, it);
    o = &bd->stack[bd->depth++];
    memset(o, 0, sizeof *o);
    o->box = it.box;
    o->layer0 = (flags & KS_BOX_WORD) != 0;
    if (o->layer0) {
        o->strong = g_array_new(FALSE, TRUE, sizeof(KsItem));
        o->morph = g_array_new(FALSE, TRUE, sizeof(KsItem));
    }
    bd->any_box = TRUE;
}

/* A layer of runs separated by spaces, inserted at *pos with a break. */
static void insert_layer(GArray *items, guint *pos, GArray *layer)
{
    KsItem brk = { 0 }, sp = { 0 };

    if (!layer->len)
        return;
    brk.type = KS_ITEM_BREAK;
    sp.type = KS_ITEM_SPACE;
    g_array_insert_val(items, (*pos)++, brk);
    for (guint i = 0; i < layer->len; i++) {
        if (i)
            g_array_insert_val(items, (*pos)++, sp);
        g_array_insert_val(items, (*pos)++, g_array_index(layer, KsItem, i));
    }
}

static void close_box(Builder *bd)
{
    Open *o;

    if (bd->depth <= 1)
        return;
    o = top(bd);
    if (o->strong) {
        guint pos = o->layer0 ? o->box->items->len : o->word_end;

        /* Trailing space of the word layer goes before the new layers. */
        while (pos > 0 && g_array_index(o->box->items, KsItem, pos - 1).type == KS_ITEM_SPACE) {
            g_array_remove_index(o->box->items, pos - 1);
            pos--;
        }
        insert_layer(o->box->items, &pos, o->strong);
        insert_layer(o->box->items, &pos, o->morph);
        g_array_free(o->strong, TRUE); /* the items moved into the box */
        g_array_free(o->morph, TRUE);
    }
    bd->depth--;
}

static void span_cb(const KsSpan *sp, void *data)
{
    Builder *bd = data;
    Open *o = top(bd);
    GArray *items = o->box->items;

    if (sp->style & KS_ST_HIDDEN)
        return;
    switch (sp->kind) {
    case KS_SPAN_BOX_OPEN:
        open_box(bd, sp->box);
        break;
    case KS_SPAN_BOX_CLOSE:
        close_box(bd);
        break;
    case KS_SPAN_TEXT:
        add_text(items, sp);
        break;
    case KS_SPAN_STRONG:
    case KS_SPAN_MORPH: {
        int len = sp->len;

        /* "subs.f.sg.a l=\"רֵאשִׁית\"": the code, not the lemma. */
        if (sp->kind == KS_SPAN_MORPH && memchr(sp->text, ' ', len))
            len = (const char *)memchr(sp->text, ' ', len) - sp->text;
        if (len <= 0)
            break;
        if (o->layer0) {
            add_run(sp->kind == KS_SPAN_STRONG ? o->strong : o->morph, sp, sp->kind, sp->text, len);
        } else {
            add(items, KS_ITEM_SPACE);
            add_run(items, sp, sp->kind, sp->text, len);
        }
        break;
    }
    case KS_SPAN_NOTE: {
        KsItem it = { 0 };

        it.type = KS_ITEM_RUN;
        it.kind = KS_SPAN_NOTE;
        it.style = sp->style;
        it.color = sp->color;
        it.text = sp->mark ? g_strndup(sp->mark, sp->mark_len)
                           : g_strdup_printf("%c", 'a' + bd->note_seq++ % 26);
        it.extra = g_strndup(sp->text, sp->len);
        g_array_append_val(items, it);
        break;
    }
    case KS_SPAN_VERSENUM:
    case KS_SPAN_XREF:
        add(items, KS_ITEM_SPACE);
        add_run(items, sp, sp->kind, sp->text, sp->len);
        add(items, KS_ITEM_SPACE);
        break;
    case KS_SPAN_PARA:
    case KS_SPAN_LINE:
        add(items, KS_ITEM_BREAK);
        if (o->layer0) { /* the word ends at this break */
            o->layer0 = FALSE;
            o->word_end = items->len - 1;
        }
        break;
    case KS_SPAN_UNKNOWN:
        break;
    }
}

gboolean ks_boxes_wanted(const char *verse, const KsStyleSheet *css)
{
    return verse && (strstr(verse, "<Q>") || ks_style_sheet_has_boxes(css));
}

KsBox *ks_boxes_build(const char *verse, const KsStyleSheet *css)
{
    Builder bd = { 0 };

    bd.root = box_new(KS_BOX_BLOCK);
    bd.stack[0].box = bd.root;
    bd.depth = 1;
    ks_gbf_parse_boxes(verse, css, span_cb, &bd);
    while (bd.depth > 1)
        close_box(&bd);
    if (!bd.any_box) {
        ks_box_free(bd.root);
        return NULL;
    }
    return bd.root;
}

static void dump(GString *g, const KsBox *b)
{
    for (guint i = 0; i < b->items->len; i++) {
        const KsItem *it = &g_array_index(b->items, KsItem, i);

        switch (it->type) {
        case KS_ITEM_SPACE:
            g_string_append_c(g, '_');
            break;
        case KS_ITEM_BREAK:
            g_string_append_c(g, '|');
            break;
        case KS_ITEM_RUN:
            if (it->kind == KS_SPAN_STRONG)
                g_string_append(g, "S:");
            else if (it->kind == KS_SPAN_MORPH)
                g_string_append(g, "M:");
            else if (it->kind == KS_SPAN_NOTE)
                g_string_append(g, "N:");
            g_string_append(g, it->text);
            break;
        case KS_ITEM_BOX: {
            const char *o = "<", *c = ">";

            if (it->box->flags & KS_BOX_WORD)
                o = "{", c = "}";
            else if (it->box->flags & KS_BOX_CELL)
                o = "[", c = "]";
            g_string_append(g, o);
            if (it->box->flags & KS_BOX_BORDER)
                g_string_append_c(g, '#');
            dump(g, it->box);
            g_string_append(g, c);
            break;
        }
        }
    }
}

char *ks_box_dump(const KsBox *b)
{
    GString *g = g_string_new(NULL);

    dump(g, b);
    return g_string_free(g, FALSE);
}
