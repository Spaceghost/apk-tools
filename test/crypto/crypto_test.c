#include <fcntl.h>
#include <string.h>

#include "apk_test.h"
#include "apk_crypto.h"
#include "vectors.h"

static void load_pkey(struct apk_pkey *pk, const char *fn, int priv)
{
	int r = apk_pkey_load(pk, AT_FDCWD, fn, priv);
	assert_int_equal(r, 0);
}

static void assert_pkey_id(const char *fn, int priv, const uint8_t *id)
{
	struct apk_pkey pk;

	load_pkey(&pk, fn, priv);
	assert_memory_equal(pk.id, id, sizeof pk.id);
	apk_pkey_free(&pk);
}

APK_TEST(pkey_identity_rsa)
{
	assert_pkey_id("vectors/rsa-pub.pem", 0, id_rsa);
	assert_pkey_id("vectors/rsa-priv.pem", 1, id_rsa);
	assert_pkey_id("vectors/rsa-priv-p8.pem", 1, id_rsa);
}

APK_TEST(pkey_identity_ec)
{
	assert_pkey_id("vectors/ec-pub.pem", 0, id_ec);
	assert_pkey_id("vectors/ec-priv.pem", 1, id_ec);
	assert_pkey_id("vectors/ec-priv-p8.pem", 1, id_ec);
}

APK_TEST(pkey_load_errors)
{
	struct apk_pkey pk;

	assert_int_equal(apk_pkey_load(&pk, AT_FDCWD, "vectors/garbage.key", 0),
			 -APKE_CRYPTO_KEY_FORMAT);
	assert_int_equal(apk_pkey_load(&pk, AT_FDCWD, "vectors/garbage.key", 1),
			 -APKE_CRYPTO_KEY_FORMAT);
	assert_int_equal(apk_pkey_load(&pk, AT_FDCWD, "vectors/nonexistent.pem", 0),
			 -ENOENT);
}

APK_TEST(digest_calc)
{
	struct apk_digest d;

	assert_int_equal(apk_digest_calc(&d, APK_DIGEST_SHA1, test_msg, strlen(test_msg)), 0);
	assert_int_equal(d.alg, APK_DIGEST_SHA1);
	assert_int_equal(d.len, 20);
	assert_memory_equal(d.data, dig_sha1, 20);

	assert_int_equal(apk_digest_calc(&d, APK_DIGEST_SHA256, test_msg, strlen(test_msg)), 0);
	assert_int_equal(d.len, 32);
	assert_memory_equal(d.data, dig_sha256, 32);

	// truncated sha256
	assert_int_equal(apk_digest_calc(&d, APK_DIGEST_SHA256_160, test_msg, strlen(test_msg)), 0);
	assert_int_equal(d.len, 20);
	assert_memory_equal(d.data, dig_sha256, 20);

	assert_int_equal(apk_digest_calc(&d, APK_DIGEST_SHA512, test_msg, strlen(test_msg)), 0);
	assert_int_equal(d.len, 64);
	assert_memory_equal(d.data, dig_sha512, 64);

	assert_int_equal(apk_digest_calc(&d, APK_DIGEST_NONE, test_msg, strlen(test_msg)),
			 -APKE_CRYPTO_ERROR);
}

APK_TEST(digest_ctx_incremental)
{
	struct apk_digest_ctx dctx;
	struct apk_digest d;
	size_t i, len = strlen(test_msg);

	assert_int_equal(apk_digest_ctx_init(&dctx, APK_DIGEST_SHA256), 0);
	for (i = 0; i < len; i++)
		assert_int_equal(apk_digest_ctx_update(&dctx, &test_msg[i], 1), 0);
	assert_int_equal(apk_digest_ctx_final(&dctx, &d), 0);
	assert_memory_equal(d.data, dig_sha256, 32);
	apk_digest_ctx_free(&dctx);
}

APK_TEST(digest_ctx_reset)
{
	struct apk_digest_ctx dctx;
	struct apk_digest d;

	assert_int_equal(apk_digest_ctx_init(&dctx, APK_DIGEST_SHA256), 0);
	assert_int_equal(apk_digest_ctx_update(&dctx, test_msg, 3), 0);
	assert_int_equal(apk_digest_ctx_reset(&dctx), 0);
	assert_int_equal(apk_digest_ctx_update(&dctx, test_msg, strlen(test_msg)), 0);
	assert_int_equal(apk_digest_ctx_final(&dctx, &d), 0);
	assert_memory_equal(d.data, dig_sha256, 32);

	// switch algorithm mid-flight
	assert_int_equal(apk_digest_ctx_reset_alg(&dctx, APK_DIGEST_SHA512), 0);
	assert_int_equal(dctx.alg, APK_DIGEST_SHA512);
	assert_int_equal(apk_digest_ctx_update(&dctx, test_msg, strlen(test_msg)), 0);
	assert_int_equal(apk_digest_ctx_final(&dctx, &d), 0);
	assert_memory_equal(d.data, dig_sha512, 64);
	apk_digest_ctx_free(&dctx);
}

static void sign_msg(struct apk_digest_ctx *dctx, uint8_t alg, struct apk_pkey *pk,
		     void *sig, size_t *len)
{
	assert_int_equal(apk_digest_ctx_init(dctx, alg), 0);
	assert_int_equal(apk_sign_start(dctx, alg, pk), 0);
	assert_int_equal(apk_digest_ctx_update(dctx, test_msg, strlen(test_msg)), 0);
	assert_int_equal(apk_sign(dctx, sig, len), 0);
}

static int verify_msg(uint8_t alg, struct apk_pkey *pk, const void *sig, size_t len)
{
	struct apk_digest_ctx dctx;
	int r;

	assert_int_equal(apk_digest_ctx_init(&dctx, alg), 0);
	assert_int_equal(apk_verify_start(&dctx, alg, pk), 0);
	assert_int_equal(apk_digest_ctx_update(&dctx, test_msg, strlen(test_msg)), 0);
	r = apk_verify(&dctx, (void *) sig, len);
	apk_digest_ctx_free(&dctx);
	return r;
}

static void verify_msg_raw(const void *msg2, size_t msg2len, uint8_t alg,
			   struct apk_pkey *pk, const void *sig, size_t len)
{
	struct apk_digest_ctx dctx;

	assert_int_equal(apk_digest_ctx_init(&dctx, alg), 0);
	assert_int_equal(apk_verify_start(&dctx, alg, pk), 0);
	assert_int_equal(apk_digest_ctx_update(&dctx, msg2, msg2len), 0);
	assert_int_not_equal(apk_verify(&dctx, (void *) sig, len), 0);
	apk_digest_ctx_free(&dctx);
}

APK_TEST(sign_rsa_roundtrip)
{
	struct apk_pkey priv, pub;
	struct apk_digest_ctx dctx;
	uint8_t sig[512];
	size_t len;

	load_pkey(&priv, "vectors/rsa-priv.pem", 1);
	load_pkey(&pub, "vectors/rsa-pub.pem", 0);

	len = sizeof sig;
	sign_msg(&dctx, APK_DIGEST_SHA256, &priv, sig, &len);
	assert_int_equal(len, 256);
	assert_int_equal(verify_msg(APK_DIGEST_SHA256, &pub, sig, len), 0);
	verify_msg_raw("apk-tools crypto conformance test vector messagX",
		       strlen(test_msg), APK_DIGEST_SHA256, &pub, sig, len);

	len = sizeof sig;
	sign_msg(&dctx, APK_DIGEST_SHA512, &priv, sig, &len);
	assert_int_equal(len, 256);
	assert_int_equal(verify_msg(APK_DIGEST_SHA512, &pub, sig, len), 0);
	apk_pkey_free(&priv);

	// same key, PKCS#8 encoded
	load_pkey(&priv, "vectors/rsa-priv-p8.pem", 1);
	len = sizeof sig;
	sign_msg(&dctx, APK_DIGEST_SHA256, &priv, sig, &len);
	assert_int_equal(verify_msg(APK_DIGEST_SHA256, &pub, sig, len), 0);
	apk_pkey_free(&priv);
	apk_pkey_free(&pub);
	apk_digest_ctx_free(&dctx);
}

APK_TEST(sign_ecdsa_roundtrip)
{
	struct apk_pkey priv, pub;
	struct apk_digest_ctx dctx;
	uint8_t sig[512];
	size_t len = sizeof sig;

	load_pkey(&priv, "vectors/ec-priv.pem", 1);
	load_pkey(&pub, "vectors/ec-pub.pem", 0);

	sign_msg(&dctx, APK_DIGEST_SHA256, &priv, sig, &len);
	assert_uint_in_range(len, 8, 139);
	assert_int_equal(verify_msg(APK_DIGEST_SHA256, &pub, sig, len), 0);
	verify_msg_raw("apk-tools crypto conformance test vector messagX",
		       strlen(test_msg), APK_DIGEST_SHA256, &pub, sig, len);
	apk_pkey_free(&priv);

	// same key, PKCS#8 encoded
	load_pkey(&priv, "vectors/ec-priv-p8.pem", 1);
	len = sizeof sig;
	sign_msg(&dctx, APK_DIGEST_SHA256, &priv, sig, &len);
	assert_int_equal(verify_msg(APK_DIGEST_SHA256, &pub, sig, len), 0);

	apk_pkey_free(&priv);
	apk_pkey_free(&pub);
}

APK_TEST(verify_rsa_vector)
{
	struct apk_pkey pk;

	load_pkey(&pk, "vectors/rsa-pub.pem", 0);
	assert_int_equal(verify_msg(APK_DIGEST_SHA256, &pk, sig_rsa_sha256, sizeof sig_rsa_sha256), 0);
	assert_int_equal(verify_msg(APK_DIGEST_SHA512, &pk, sig_rsa_sha512, sizeof sig_rsa_sha512), 0);

	// tampered message
	verify_msg_raw("apk-tools crypto conformance test vector messagX",
		       strlen(test_msg), APK_DIGEST_SHA256, &pk,
		       sig_rsa_sha256, sizeof sig_rsa_sha256);

	// tampered signature
	{
		uint8_t bad[sizeof sig_rsa_sha256];
		memcpy(bad, sig_rsa_sha256, sizeof bad);
		bad[100] ^= 1;
		assert_int_not_equal(verify_msg(APK_DIGEST_SHA256, &pk, bad, sizeof bad), 0);

		// truncated signature
		assert_int_not_equal(verify_msg(APK_DIGEST_SHA256, &pk, bad, sizeof bad - 1), 0);
	}
	apk_pkey_free(&pk);
}

APK_TEST(verify_ecdsa_vector)
{
	struct apk_pkey pk;

	load_pkey(&pk, "vectors/ec-pub.pem", 0);
	assert_int_equal(verify_msg(APK_DIGEST_SHA256, &pk, sig_ec_sha256, SIG_EC_SHA256_LEN), 0);

	{
		uint8_t bad[SIG_EC_SHA256_LEN];
		memcpy(bad, sig_ec_sha256, sizeof bad);
		bad[10] ^= 1;
		assert_int_not_equal(verify_msg(APK_DIGEST_SHA256, &pk, bad, sizeof bad), 0);
	}
	apk_pkey_free(&pk);
}

APK_TEST(verify_wrong_key_type)
{
	struct apk_pkey pub_rsa, pub_ec;

	load_pkey(&pub_rsa, "vectors/rsa-pub.pem", 0);
	load_pkey(&pub_ec, "vectors/ec-pub.pem", 0);

	assert_int_not_equal(verify_msg(APK_DIGEST_SHA256, &pub_ec,
					sig_rsa_sha256, sizeof sig_rsa_sha256), 0);
	assert_int_not_equal(verify_msg(APK_DIGEST_SHA256, &pub_rsa,
					sig_ec_sha256, SIG_EC_SHA256_LEN), 0);

	apk_pkey_free(&pub_rsa);
	apk_pkey_free(&pub_ec);
}
