/* SPDX-License-Identifier: GPL-3.0-only */
/* STRUMLINE project: one flash object (OBJ_PROJ): the settings and the progression. A project of
 * another format, or none, gives the defaults. Part of the unity build, after app.h. */
project_t proj;

void project_defaults(void)
{
    int i;
    memset(&proj, 0, sizeof proj);
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    for (i = 0; i < P_NPARAMS; i++)
        proj.par[i] = om_param_info(i)->def;
    proj.pat_last = 5;                           /* Folk */
    proj.leds = 1;
    /* a progression to start from: I V vi IV */
    proj.prog[0] = 0 + 1;
    proj.prog[2] = 4 + 1;
    proj.prog[4] = 5 + 1;
    proj.prog[6] = 3 + 1;
}

static int project_valid(void)
{
    int i;
    if (proj.magic != PROJ_MAGIC || proj.format != PROJ_FORMAT)
        return 0;
    for (i = 0; i < P_NPARAMS; i++) {
        const om_param_t *p = om_param_info(i);
        if (proj.par[i] < p->lo || proj.par[i] > p->hi)
            return 0;
    }
    for (i = 0; i < OM_SLOTS; i++)
        if (proj.prog[i] > OM_NCODES)
            return 0;
    return proj.leds <= 1u && proj.pat_last >= 1u && proj.pat_last < NPATTERNS;
}

int project_load(void)
{
    int n, i;
    memset(&proj, 0, sizeof proj);
    n = plat_store_load(OBJ_PROJ, &proj, sizeof proj);
    if (!(n >= 8 && project_valid()))
        project_defaults();
    for (i = 0; i < OM_SLOTS; i++)
        om_prog[i] = proj.prog[i];
    return n >= 8 && proj.magic == PROJ_MAGIC ? 0 : -1;
}

int project_save(void)
{
    int i;
    for (i = 0; i < OM_SLOTS; i++)
        proj.prog[i] = om_prog[i];
    return plat_store_save(OBJ_PROJ, &proj, sizeof proj);
}

void project_apply(void)
{
    int i;
    for (i = 0; i < P_NPARAMS; i++)
        om_set(i, proj.par[i]);
}
