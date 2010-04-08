#define DDEBUG 0
#include "ddebug.h"#include <ndk.h>
#include "ngx_http_set_base32.h"


ngx_int_t
ngx_http_set_misc_encode_base32(ngx_http_request_t *r,
        ngx_str_t *res, ngx_http_variable_value_t *v)
{
    size_t                   len;
    u_char                  *p;
    u_char                  *src, *dst;

    len = base32_encoded_length(v->len);

    dd("estimated dst len: %d", len);

    p = ngx_palloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    src = v->data; dst = p;

    encode_base32((int)v->len, (const char *)src, (int *)&len, (char *)dst);

    res->data = p;
    res->len = len;

    dd("res (len %d): %.*s", res->len, res->len, res->data);

    return NGX_OK;
}


ngx_int_t
ngx_http_set_misc_decode_base32(ngx_http_request_t *r,
        ngx_str_t *res, ngx_http_variable_value_t *v)
{
    size_t                   len;
    u_char                  *p;
    u_char                  *src, *dst;
    int                      ret;

    len = base32_decoded_length(v->len);

    dd("estimated dst len: %d", len);

    p = ngx_palloc(r->pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    src = v->data; dst = p;

    ret = decode_base32((int)v->len, (const char *)src, (int *)&len,
            (char *)dst);

    if (ret == 0 /* OK */) {
        res->data = p;
        res->len = len;

        return NGX_OK;
    }

    /* failed to decode */

    res->data = NULL;
    res->len = 0;

    return NGX_OK;
}


/* ʵ�ֲο� src/core/ngx_string.c �е� ngx_(encode|decode)_base64() ���� */

/**
 * �������ַ���ת���ɶ�Ӧ�� Base32 ������ʽ. Ŀ���ַ������뱣֤�г���Ŀռ����ɱ���������.
 * �����ú� base32_encoded_length() Ԥ����������ݳ��Ȳ�Ԥ��ΪĿ���ַ�������ռ�.
 * <code>
 * 	char *src, *dst;
 * 	int slen, dlen;
 * 	slen = sizeof("hello") - 1;
 * 	src = (char*) "hello";
 * 	dst = malloc(base32_encoded_length(slen));
 * 	encode_base32(slen, src, &(dlen), dst);
 * </code>
 * @param slen Դ���ݴ�����.
 * @param src ԭ���ݴ�ָ��.
 * @param dlen Ŀ�����ݴ�����ָ��, ���� Base32 ��������ݳ���.
 * @param dst Ŀ�����ݴ�ָ��, ���� Base32 ���������.
 * */
void
encode_base32(int slen, const char *src, int *dlen, char *dst)
{
	static unsigned char basis32[] = "0123456789abcdefghijklmnopqrstuv";

	int len;
	const unsigned char *s;
	unsigned char *d;

	len = slen;
	s = (const unsigned char*)src;
	d = (unsigned char*)dst;

	while (len > 4) {
		/*
		 * According to RFC 3548, The layout for input data is:
		 *
		 *  Lower Addr --------------------> Higher Addr
		 * 	msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb
		 * 	76543210 76543210 76543210 76543210 76543210
		 *
		 * After segmenting:
		 *
		 * 	Lower Addr -----------------------------------------------> Higher Addr
		 * 	msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb
		 * 	---76543 ---21076 ---54321 ---07654 ---32107 ---65432 ---10765 ---43210
		 *
		 * */
		*d++ = basis32[s[0] >> 3];
		*d++ = basis32[((s[0] & 0x07) << 2) | (s[1] >> 6)];
		*d++ = basis32[(s[1] >> 1) & 0x1f];
		*d++ = basis32[((s[1] & 1) << 4) | (s[2] >> 4)];
		*d++ = basis32[((s[2] & 0x0f) << 1) | (s[3] >> 7)];
		*d++ = basis32[(s[3] >> 2) & 0x1f];
		*d++ = basis32[((s[3] & 0x03) << 3) | (s[4] >> 5)];
		*d++ = basis32[s[4] & 0x1f];

		s += 5;
		len -= 5;
	}

	/* ����� 5 ��������ʣ���ֽڴ� */
	/**
	 * Remain 1 byte:
	 *
	 * 	Lower Addr -----------------------------------------------> Higher Addr
	 * 	msb  lsb msb  lsb padding0 padding1 padding2 padding3 padding4 padding5
	 * 	---76543 ---210-- ======== ======== ======== ======== ======== ========
	 *
	 * Remain 2 bytes:
	 *
	 * 	Lower Addr -----------------------------------------------> Higher Addr
	 * 	msb  lsb msb  lsb msb  lsb msb  lsb padding0 padding1 padding2 padding3
	 * 	---76543 ---21076 ---54321 ---0---- ======== ======== ======== ========
	 *
	 * Remain 3 bytes:
	 *
	 * 	Lower Addr -----------------------------------------------> Higher Addr
	 * 	msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb padding0 padding1 padding2
	 * 	---76543 ---21076 ---54321 ---07654 ---3210- ======== ======== ========
	 *
	 * Remain 4 bytes:
	 *
	 * 	Lower Addr -----------------------------------------------> Higher Addr
	 * 	msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb padding0
	 * 	---76543 ---21076 ---54321 ---07654 ---32107 ---65432 ---10--- ========
	 *
	 **/
	if (len) {
		*d++ = basis32[s[0] >> 3];

		if (len == 1) {
			/* ʣ�� 1 ���ֽ� */
			*d++ = basis32[(s[0] & 0x07) << 2];

			/* ������Ϊֹ�� 6 �� = */
			*d++ = '=';
			*d++ = '=';
			*d++ = '=';
			*d++ = '=';
			*d++ = '=';
		} else {
			*d++ = basis32[((s[0] & 0x07) << 2) | (s[1] >> 6)];
			*d++ = basis32[(s[1] >> 1) & 0x1f];

			if (len == 2) {
				/* ʣ�� 2 ���ֽ� */
				*d++ = basis32[(s[1] & 1) << 4];

				/* ������Ϊֹ�� 4 �� = */
				*d++ = '=';
				*d++ = '=';
				*d++ = '=';
			} else {
				*d++ = basis32[((s[1] & 1) << 4) | (s[2] >> 4)];

				if (len == 3) {
					/* ʣ�� 3 ���ֽ� */
					*d++ = basis32[(s[2] & 0x0f) << 1];

					/* ������Ϊֹ�� 3 �� = */
					*d++ = '=';
					*d++ = '=';
				} else {
					/* ʣ�� 4 ���ֽ� */
					*d++ = basis32[((s[2] & 0x0f) << 1) | (s[3] >> 7)];
					*d++ = basis32[(s[3] >> 2) & 0x1f];
					*d++ = basis32[(s[3] & 0x03) << 3];

					/* ������Ϊֹ�� 1 �� = */
				}
			}
		}

		*d++ = '=';
	}

	*dlen = d - (unsigned char*)dst;
}


/**
 * �������� Base32 ����(��Сд������)����ת���ɶ�Ӧ��ԭʼ�ַ���. Ŀ���ַ������뱣֤�г���Ŀռ����ɽ���������.
 * �����ú� base32_decoded_length() Ԥ����������ݳ��Ȳ�Ԥ��ΪĿ���ַ�������ռ�.
 * <code>
 * 	char *src, *dst;
 * 	int slen, dlen;
 * 	slen = sizeof("6RRU6H5CI3======") - 1;
 * 	src = (char*)"6RRU6H5CI3======";
 * 	dst = malloc(base32_decoded_length(slen));
 * 	if (!decode_base32(slen, src, &(dlen), dst)) {
 * 		// do something with dst
 * 	} else {
 * 		// decoding error
 * 	}
 * </code>
 * @param slen Դ���ݴ�����.
 * @param src Դ���ݴ�ָ��.
 * @param dlen Ŀ�����ݳ���ָ��, ���� Base32 ���������ݳ���.
 * @param dst Ŀ�����ݴ�ָ��, ���� Base32 ����������.
 * @retval ����ɹ�ʱ����0��ʧ��ʱ����-1.
 * */
int
decode_base32(int slen, const char *src, int *dlen, char *dst)
{
	static unsigned char basis32[] = {
		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 0 - 15 */
		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 16 - 31 */
		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 32 - 47 */
		 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 77, 77, 77, 77, 77, 77, 	/* 48 - 63 */
		77, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 	/* 64 - 79 */
		25, 26, 27, 28, 29, 30, 31, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 80 - 95 */
		77, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 	/* 96 - 111 */
		25, 26, 27, 28, 29, 30, 31, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 112 - 127 */

		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 128 - 143 */
		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 144 - 159 */
		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 160 - 175 */
		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 176 - 191 */
		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 192 - 207 */
		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 208 - 223 */
		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 	/* 224 - 239 */
		77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77, 77 		/* 240 - 255 */
	};

	int len, mod;
	const unsigned char *s = (const unsigned char*)src;
	unsigned char *d = (unsigned char*)dst;

	for (len = 0; len < slen; len++) {
		if (s[len] == '=') {
			break;
		}

		if (basis32[s[len]] == 77) {
			return -1;
		}
	}

	mod = len % 8;

	if (mod == 1 || mod == 3 || mod == 6) {
		/* Base32 ���봮��Ч���ȴ��� */
		return -1;
	}

	while (len > 7) {
		/*
		 * According to RFC 3548, The layout for input data is:
		 *
		 * 	Lower Addr -----------------------------------------------> Higher Addr
		 * 	msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb
		 * 	---76543 ---21076 ---54321 ---07654 ---32107 ---65432 ---10765 ---43210
		 *
		 * After assembling:
		 *
		 *  Lower Addr --------------------> Higher Addr
		 * 	msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb
		 * 	76543210 76543210 76543210 76543210 76543210
		 *
		 * */
		*d++ = (basis32[s[0]] << 3) | ((basis32[s[1]] >> 2) & 0x07);
		*d++ = ((basis32[s[1]] & 0x03) << 6) | (basis32[s[2]] << 1) | ((basis32[s[3]] >> 4) & 1);
		*d++ = ((basis32[s[3]] & 0x0f) << 4) | ((basis32[s[4]] >> 1) & 0x0f);
		*d++ = ((basis32[s[4]] & 1) << 7) | ((basis32[s[5]] & 0x1f) << 2) | ((basis32[s[6]] >> 3) & 0x03);
		*d++ = ((basis32[s[6]] & 0x07) << 5) | (basis32[s[7]] & 0x1f);

		s += 8;
		len -= 8;
	}

	/* ����� 8 �������� Base32 ���봮 */
	/**
	 * Remain 1 byte:
	 *
	 * 	Lower Addr -----------------------------------------------> Higher Addr
	 * 	msb  lsb msb  lsb padding0 padding1 padding2 padding3 padding4 padding5
	 * 	---76543 ---210-- ======== ======== ======== ======== ======== ========
	 *
	 * Remain 2 bytes:
	 *
	 * 	Lower Addr -----------------------------------------------> Higher Addr
	 * 	msb  lsb msb  lsb msb  lsb msb  lsb padding0 padding1 padding2 padding3
	 * 	---76543 ---21076 ---54321 ---0---- ======== ======== ======== ========
	 *
	 * Remain 3 bytes:
	 *
	 * 	Lower Addr -----------------------------------------------> Higher Addr
	 * 	msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb padding0 padding1 padding2
	 * 	---76543 ---21076 ---54321 ---07654 ---3210- ======== ======== ========
	 *
	 * Remain 4 bytes:
	 *
	 * 	Lower Addr -----------------------------------------------> Higher Addr
	 * 	msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb msb  lsb padding0
	 * 	---76543 ---21076 ---54321 ---07654 ---32107 ---65432 ---10--- ========
	 *
	 **/
	if (len) {
		/* ʣ�� 2 ���ֽ� */
		*d++ = (basis32[s[0]] << 3) | ((basis32[s[1]] >> 2) & 0x07);

		if (len > 2) {
			/* ʣ�� 4 ���ֽ� */
			*d++ = ((basis32[s[1]] & 0x03) << 6) | ((basis32[s[2]] & 0x1f) << 1) | ((basis32[s[3]] >> 4) & 1);

			if (len > 4) {
				/* ʣ�� 5 ���ֽ� */
				*d++ = ((basis32[s[3]] & 0x0f) << 4) | ((basis32[s[4]] >> 1) & 0x0f);

				if (len > 5) {
					/* ʣ�� 7 ���ֽ� */
					*d++ = ((basis32[s[4]] & 1) << 7) | ((basis32[s[5]] & 0x1f) << 2) | ((basis32[s[6]] >> 3) & 0x03);
				}
			}
		}
	}

	*dlen = d - (unsigned char*)dst;

	return 0;
}

/* vi:ts=4 sw=4 */

