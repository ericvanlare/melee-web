/* GALE01's GXSetTevClampMode is a four-byte return instruction.  The SDK
 * source assertion is not present in the retail executable, so preserve the
 * retail no-op instead of introducing a browser-only draw failure. */
void GXSetTevClampMode(int stage,int mode)
{
    (void)stage; (void)mode;
}
