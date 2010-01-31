/*
 * The contents of this file are subject to the Mozilla Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is SHA 180-1 Header File
 *
 * The Initial Developer of the Original Code is Paul Kocher of
 * Cryptography Research.  Portions created by Paul Kocher are
 * Copyright (C) 1995-9 by Cryptography Research, Inc.  All
 * Rights Reserved.
 *
 * Contributor(s):
 *
 *     Paul Kocher
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License Version 2 or later (the
 * "GPL"), in which case the provisions of the GPL are applicable
 * instead of those above.  If you wish to allow use of your
 * version of this file only under the terms of the GPL and not to
 * allow others to use your version of this file under the MPL,
 * indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by
 * the GPL.  If you do not delete the provisions above, a recipient
 * may use your version of this file under either the MPL or the
 * GPL.
 */

typedef struct {
  unsigned int H[5];
  unsigned int W[80];
  int lenW;
  unsigned int sizeHi,sizeLo;
} moz_SHA_CTX;

void moz_SHA1_Init(moz_SHA_CTX *ctx);
void moz_SHA1_Update(moz_SHA_CTX *ctx, const void *dataIn, int len);
void moz_SHA1_Final(unsigned char hashout[20], moz_SHA_CTX *ctx);

#define git_SHA_CTX	moz_SHA_CTX
#define git_SHA1_Init	moz_SHA1_Init
#define git_SHA1_Update	moz_SHA1_Update
#define git_SHA1_Final	moz_SHA1_Final
