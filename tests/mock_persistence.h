/* Software-only lease fixtures: explicit no-I/O persistence seam.
 * Real atomic-file/crash behavior belongs to lease_persistence_test.c. */
#pragma once
static int mock_persist_free(sosc_state_t *state)
{
    (void) state;
    return 0;
}
