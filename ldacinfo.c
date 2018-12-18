/*
 * display ldac info
 *
 * url: https://github.com/eggman/ldacenc
 * licence: Apache License, Version 2.0
 */

#include <stdio.h>
#include "table.h"

static int g_sfc_weight;
static int g_nadjqus;
int g_a_idsf[LDAC_MAXNQUS];
int g_a_grad[LDAC_MAXNQUS];
int g_a_addwl[LDAC_MAXNQUS];
int g_a_idwl1[LDAC_MAXNQUS];
int g_a_idwl2[LDAC_MAXNQUS];
AB g_ab;

int read_bit(unsigned char *pdata, int pos)
{
    int bytepos = pos / 8;
    int bitpos  = pos % 8;
    return (pdata[bytepos] & (1 << (7 - bitpos))) ? 1 : 0;
}

int read_bits(unsigned char *pdata, int pos, int nbits)
{
    int tmp = 0;
    int p = pos;

	for (int i = 1; i <= nbits; i++) {
        tmp = tmp << 1;
	    tmp += read_bit(pdata, p);
        p++;
    }
    return tmp;
}

int dump_ldac_header(unsigned char *pdata, int pos)
{
    int syncword;

    syncword = read_bits(pdata, pos + 0, 8);
    if (syncword != 0xAA) {
        printf("not found syncword\n");
        return -1;
    }

    printf("HEADER\n");
    printf("  SYNCWORD   %02X\n", syncword);
    printf("  SAMPLERATE %02X\n", read_bits(pdata, pos +  8, 3));
    printf("  CHCONFIG   %02X\n", read_bits(pdata, pos + 11, 2));
    printf("  FRAMELEN   %02X\n", read_bits(pdata, pos + 13, 9));
    printf("  FRAMESTAT  %02X\n", read_bits(pdata, pos + 22, 2));
    printf("\n");

    return pos + 24;
}

int dump_band_info_ldac(AB *p_ab, unsigned char *pdata, int pos)
{
    printf("BANDINFO\n");
    printf("  NBAND      %02X\n", read_bits(pdata, pos + 0, 4));
    p_ab->nbands = 2 + read_bits(pdata, pos + 0, 4);
    printf("  FLAG       %02X\n", read_bits(pdata, pos + 4, 1));
    printf("\n");

    return pos + 5;
}

int dump_gradient_ldac(AB *p_ab, unsigned char *pdata, int pos)
{
    int new_pos = pos;
    p_ab->grad_mode = read_bits(pdata, pos + 0, 2);

    printf("GRADIENT\n");
        printf("  GRADMODE   %02X\n", read_bits(pdata, pos + 0, 2));
    if (p_ab->grad_mode == 0) {
        printf("  GRADQU0_L  %02X\n", read_bits(pdata, pos +  2, 6));
        printf("  GRADQU0_H  %02X\n", read_bits(pdata, pos +  8, 6));
        printf("  GRADOS_L   %02X\n", read_bits(pdata, pos + 14, 5));
        printf("  GRADOS_H   %02X\n", read_bits(pdata, pos + 19, 5));
        printf("  NADJQU     %02X\n", read_bits(pdata, pos + 24, 5));
        g_nadjqus = read_bits(pdata, pos + 24, 5);
        p_ab->nadjqus = read_bits(pdata, pos + 24, 5);
        new_pos = pos + 29;

        int hqu = 24;
        int iqu;
        int grad_qu_l = read_bits(pdata, pos +  2, 6);
        int grad_qu_h = read_bits(pdata, pos +  8, 6) + 1;
        int grad_os_l = read_bits(pdata, pos + 14, 5);
        int grad_os_h = read_bits(pdata, pos + 19, 5);
        int tmp = grad_qu_h - grad_qu_l;
        int *p_grad = g_a_grad;
        const unsigned char *p_t;

        /* Calculate Gradient Curve */
        for (iqu = 0; iqu < grad_qu_h; iqu++) {
            p_grad[iqu] = -grad_os_l;
        }
        for (iqu = grad_qu_h; iqu < hqu; iqu++) {
            p_grad[iqu] = -grad_os_h;
        }

        if (tmp > 0) {
            p_t = gaa_resamp_grad_ldac[tmp-1];

            tmp = grad_os_h - grad_os_l;
            if (tmp > 0) {
                tmp = tmp-1;
                for (iqu = grad_qu_l; iqu < grad_qu_h; iqu++) {
                    p_grad[iqu] -= ((*p_t++ * tmp) >> 8) + 1;
                }
            }
            else if (tmp < 0) {
                tmp = -tmp-1;
                for (iqu = grad_qu_l; iqu < grad_qu_h; iqu++) {
                    p_grad[iqu] += ((*p_t++ * tmp) >> 8) + 1;
                }
            }
        }
        printf("  a_grad   ");
        for (iqu = 0; iqu < 24; iqu++ ) {
            printf(" %d", p_grad[iqu]);
        }
        printf("\n");

    } else {
        printf("  GRADQU1    %02X\n", read_bits(pdata, pos +  2, 5));
        printf("  GRADOS     %02X\n", read_bits(pdata, pos +  7, 5));
        printf("  NADJQU     %02X\n", read_bits(pdata, pos + 12, 5));
        g_nadjqus = read_bits(pdata, pos + 12, 5);
        p_ab->nadjqus = read_bits(pdata, pos + 12, 5);
        new_pos = pos + 17;
    }
    printf("\n");

    return new_pos;
}

int decode_huffman(const CODES c, unsigned char *pdata, int start_pos)
{
    int pos = start_pos;
    int tmp;
    int i;

    /* read 1st bit */
    tmp =  read_bit(pdata, pos);

    /* check codes */
    for (int nbits = c.codes_min_bits; nbits <= c.codes_max_bits; nbits++) {
        pos++;
        tmp = tmp << 1;
        tmp += read_bit(pdata, pos);
        for (i=0; i < c.ncodes; i++) {
            if (c.p_tbl[i].code == tmp) {
                goto found;
            }
        }
    }
    puts("not found");
    return -1;
found:

    return i;
}

int dump_ldac_sfhuffman(unsigned char *pdata, int pos, const CODES c)
{
    int p, count, idx;
    int dif[LDAC_MAXNQUS];

    printf("  DIF       ");
    for (p = pos, count = 1; count < 24;) {
        idx = decode_huffman(c, pdata, p);
        if (idx >= 0) {
            if (idx & (c.ncodes>>1)) {
                dif[count] = -c.ncodes + idx;
            } else {
                dif[count] = idx;
            }
            printf(" %2d", dif[count]);
            p += c.p_tbl[idx].len;
            count++;
        } else {
            break;
        }
    }
    printf("\n");

    //todo support scale_facotor_2
    //dump idsf
    int val0 = g_a_idsf[0];
    int val1;
    const unsigned char *p_tbl;

    p_tbl = gaa_sfcwgt_ldac[g_sfc_weight];
    g_a_idsf[0] = val0 - p_tbl[0];

    for (int iqu = 1; iqu < 24; iqu++) {
        val1 = dif[iqu] + val0;
        g_a_idsf[iqu] = val1 - p_tbl[iqu];
        val0 = val1;
    }

    printf("  a_idsf    ");
    for (int i = 0; i < 24 /* LDAC_MAXNQUS */; i++) {
        printf(" %02X", g_a_idsf[i]);
    }
    printf("\n");

    return p - pos;
}

int dump_ldac_scalefactor(unsigned char *pdata, int pos)
{
    int new_pos, sfc_mode, sfc_bitlen, sfc_offset, hc_len;
    printf("SCALEFACTOR\n");
    printf("  SFCMODE    %02X\n", read_bits(pdata, pos +  0, 1));
    sfc_mode = read_bits(pdata, pos +  0, 1);

    if (sfc_mode == 0) {
        printf("  SFCBLEN    %02X\n", read_bits(pdata, pos +  1, 2));
        sfc_bitlen = 3 + read_bits(pdata, pos +  1, 2);
        printf("  IDSF       %02X\n", read_bits(pdata, pos +  3, 5));
        sfc_offset = read_bits(pdata, pos +  3, 5);
        printf("  SFCWTBL    %02X\n", read_bits(pdata, pos +  8, 3));
        g_sfc_weight = read_bits(pdata, pos +  8, 3);

        printf("  VAL0       %02X\n", read_bits(pdata, pos + 11, sfc_bitlen));
        g_a_idsf[0] = read_bits(pdata, pos + 11, sfc_bitlen) + sfc_offset;

        hc_len = dump_ldac_sfhuffman(pdata, pos + 11 + sfc_bitlen, codes0);
        new_pos = pos + 11 + sfc_bitlen + hc_len;
    } else {
        /* scale_factor 2 */
        printf("  SFCBLEN    %02X\n", read_bits(pdata, pos +  1, 2));
        sfc_bitlen = 2 + read_bits(pdata, pos +  1, 2);

        /* decode huffman */
        hc_len = dump_ldac_sfhuffman(pdata, pos + 3, codes1);
        new_pos = 3 + hc_len;
    }

    printf("\n");

    return new_pos;
}

int dump_ldac_spectrum(unsigned char *pdata, int pos)
{
    int i, iqu, idwl1, idwl2;
    int hqu = 24;
    int nqus = 24;
    int isp;
    int lsp, hsp;
    int nsps, wl;
    int *p_grad, *p_idsf, *p_addwl, *p_idwl1, *p_idwl2, *p_tmp;

    printf("SPECTRUM\n");

    p_grad = g_a_grad;
	p_idsf = g_a_idsf;
	p_addwl = g_a_addwl;
	p_idwl1 = g_a_idwl1;
	p_idwl2 = g_a_idwl2;

    /* grad_mode == 0 */
    for (iqu = 0; iqu < hqu; iqu++) {
        for (iqu = 0; iqu < hqu; iqu++) {
            idwl1 = p_idsf[iqu] + p_grad[iqu];
            if (idwl1 < LDAC_MINIDWL1) {
                idwl1 = LDAC_MINIDWL1;
            }
            idwl2 = 0;
            if (idwl1 > LDAC_MAXIDWL1) {
                idwl2 = idwl1 - LDAC_MAXIDWL1;
                if (idwl2 > LDAC_MAXIDWL2) {
                    idwl2 = LDAC_MAXIDWL2;
                }
                idwl1 = LDAC_MAXIDWL1;
            }
            p_idwl1[iqu] = idwl1;
            p_idwl2[iqu] = idwl2;
        }
    }

    printf("  a_idwl1 a ");
    for (iqu = 0; iqu < 24 /* LDAC_MAXNQUS */; iqu++) {
        printf(" %02X", p_idwl1[iqu]);
    }
    printf("\n");

    /* adjust  */
    int nadjqus = g_nadjqus;
	p_tmp = g_a_idwl1;
    for (iqu = 0; iqu < nqus; iqu++) {
        idwl1 = p_tmp[iqu];
        if (iqu < nadjqus) {
            idwl1++;
        }
        idwl2 = 0;
        if (idwl1 > LDAC_MAXIDWL1) {
            idwl2 = idwl1 - LDAC_MAXIDWL1;
            if (idwl2 > LDAC_MAXIDWL2) {
                idwl2 = LDAC_MAXIDWL2;
            }
            idwl1 = LDAC_MAXIDWL1;
        }
        p_idwl1[iqu] = idwl1;
        p_idwl2[iqu] = idwl2;
    }

    printf("  a_idwl1 b ");
    for (iqu = 0; iqu < 24 /* LDAC_MAXNQUS */; iqu++) {
        printf(" %02X", p_idwl1[iqu]);
    }
    printf("\n");

    /* wl */
    int p_wl[LDAC_MAXLSU];
    int j = 0;
    printf("  wl        ");
    for (iqu = 0; iqu < nqus; iqu++) {
        lsp = ga_isp_ldac[iqu];
        hsp = ga_isp_ldac[iqu+1];
        nsps = ga_nsps_ldac[iqu];
        idwl1 = g_a_idwl1[iqu];
        wl = ga_wl_ldac[idwl1];

        if (idwl1 == 1) {
            isp = lsp;

            if (nsps == 2) {
                printf(" %2d", LDAC_2DIMSPECBITS);
                p_wl[j] = LDAC_2DIMSPECBITS;
                j++;
            } else {
                for (i = 0; i < nsps>>2; i++, isp+=4) {
                   printf(" %2d", LDAC_4DIMSPECBITS);
                   p_wl[j] = LDAC_4DIMSPECBITS;
                   j++;
                }
            }
        } else {
            for (isp = lsp; isp < hsp; isp++) {
                printf(" %2d", wl);
                p_wl[j] = wl;
                j++;
            }
        }
    }
    printf("\n");

    // spectrum
    int nsp = j;

    printf(" coded spec");
    for (i = 0; i < nsp; i++) {
        printf(" %03X", read_bits(pdata, pos, p_wl[i]));
        pos += p_wl[i];
    }
    printf("\n");
    printf("\n");

    return pos;
}

int dump_ldac_residual(unsigned char *pdata, int pos)
{
    int iqu, isp;
    int lsp, hsp;
    int nqus = 24;
    int idwl2, wl;
    int *p_idwl2;

    p_idwl2 = g_a_idwl2;

    printf("RESIDUAL\n");


    printf("  a_idwl2   "); for (iqu = 0; iqu < 24 /* LDAC_MAXNQUS */; iqu++) {
        printf(" %02X", p_idwl2[iqu]);
    }
    printf("\n");

    printf(" coded spec");
    for (iqu = 0; iqu < nqus; iqu++) {
        idwl2 = p_idwl2[iqu];

        if (idwl2 > 0) {
            lsp = ga_isp_ldac[iqu];
            hsp = ga_isp_ldac[iqu+1];
            wl = ga_wl_ldac[idwl2];

            for (isp = lsp; isp < hsp; isp++) {
                //pack_store_ldac(p_ac->a_rspec[isp], wl, p_stream, p_loc);
                printf(" %03X", read_bits(pdata, pos, wl));
                pos += wl;
            }
        }
    }
    printf("\n");
    printf("\n");

    return pos;
}

int main(int argc, char *argv[])
{
    int pos;
    unsigned char ldac[1024];
    FILE *infp;
    AB *p_ab;
    p_ab = &g_ab;

    if ((infp = fopen(argv[1], "r"))==NULL) {
        printf("can't open input file\n");
        return -1;
    }

    fread(ldac, 660, 1, infp);

    pos = dump_ldac_header(ldac, 0);
    pos = dump_band_info_ldac(p_ab, ldac, pos);
    pos = dump_gradient_ldac(p_ab, ldac, pos);
    pos = dump_ldac_scalefactor(ldac, pos);
    pos = dump_ldac_spectrum(ldac, pos);
    pos = dump_ldac_residual(ldac, pos);
    pos = dump_ldac_scalefactor(ldac, pos);

    return 0;
}

