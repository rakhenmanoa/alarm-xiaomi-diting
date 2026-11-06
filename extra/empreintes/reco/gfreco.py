#!/usr/bin/python3 -I
"""Reconnaissance d'empreintes côté Linux, à partir des captures brutes Goodix.

Dépendances : python-numpy, python-scipy, python-opencv (rien d'autre).

  gfreco.py ref    CAPTURE_SANS_DOIGT.bin          enregistre la référence du capteur
  gfreco.py enroll NOM CAPTURE.bin [CAPTURE.bin…]  ajoute des modèles au doigt NOM
  gfreco.py verify CAPTURE.bin [NOM]               compare ; code retour 0 = reconnu
  gfreco.py score  A.bin B.bin                     score brut entre deux captures
  gfreco.py eval   DOSSIER                         courbes genuine/imposteur (voir README)

Une capture est un fichier « GF_DUMP=… 1003 21 921640 » (voir gf-image.py).
La base est dans $GFRECO_DIR (défaut ~/.local/share/gfreco), en 0600 : les
modèles sont des images de crêtes, donc des données biométriques. Ne rien
publier de ce dossier.
"""
import os
import struct
import sys

import cv2
import numpy as np

W, H, NF = 192, 224, 5
DB = os.environ.get('GFRECO_DIR', os.path.expanduser('~/.local/share/gfreco'))
# Réglages (voir README) : le seuil est une valeur de départ, à recalibrer
# avec `eval` sur de vraies captures.
THRESH = float(os.environ.get('GFRECO_THRESH', '0.30'))
ANGLES = np.arange(-30, 31, 5)       # rotations testées (degrés)
MAXSHIFT = 40                        # décalage max (pixels)


def load_frames(path):
    d = open(path, 'rb').read()
    w, h = struct.unpack_from('<II', d, 28)
    if (w, h) != (W, H):
        raise SystemExit(f'{path} : géométrie {w}×{h} inattendue')
    n = w * h
    fr = np.frombuffer(d, '<u2', offset=36, count=NF * n).reshape(NF, h, w)
    return fr.astype(np.float32)


def mean_frame(path):
    return load_frames(path).mean(0)


def ref_path():
    return os.path.join(DB, 'ref.npy')


def flat(img, ref):
    """Mise à plat : rapport au cliché sans doigt (efface la grille du capteur)."""
    r = ref - ref.min()
    i = img - img.min()
    r = cv2.GaussianBlur(r, (0, 0), 0.8) + 1.0
    i = cv2.GaussianBlur(i, (0, 0), 0.8) + 1.0
    return np.log(i / r)


def ridge_image(path, ref):
    """Image de crêtes normalisée, et masque de validité."""
    x = flat(mean_frame(path), ref)
    x = x - cv2.GaussianBlur(x, (0, 0), 7)
    sd = np.sqrt(cv2.GaussianBlur(x * x, (0, 0), 12)) + 1e-6
    x = x / sd
    # bande passante : crêtes
    x = cv2.GaussianBlur(x, (0, 0), 1.0) - cv2.GaussianBlur(x, (0, 0), 3.5)
    # masque : zone où la texture existe et hors des bords du capteur
    e = cv2.GaussianBlur(np.abs(x), (0, 0), 6)
    m = (e > 0.6 * np.median(e)).astype(np.float32)
    m[:14] = m[-14:] = 0
    m[:, :14] = m[:, -14:] = 0
    m = cv2.erode(m, np.ones((9, 9), np.uint8))
    return x.astype(np.float32), m


def gabor_enhance(x, m):
    """Renforce les crêtes : filtre de Gabor orienté selon le champ d'orientation local."""
    gx = cv2.Sobel(x, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(x, cv2.CV_32F, 0, 1, ksize=3)
    a = cv2.GaussianBlur(gx * gx - gy * gy, (0, 0), 6)
    b = cv2.GaussianBlur(2 * gx * gy, (0, 0), 6)
    theta = 0.5 * np.arctan2(b, a) + np.pi / 2       # direction des crêtes
    nb = 12
    bank = []
    for k in range(nb):
        t = np.pi * k / nb
        kern = cv2.getGaborKernel((17, 17), 3.5, t + np.pi / 2, 7.0, 1.0, 0)
        kern -= kern.mean()
        bank.append(cv2.filter2D(x, cv2.CV_32F, kern))
    bank = np.stack(bank)
    idx = (np.mod(theta, np.pi) / np.pi * nb).round().astype(int) % nb
    y = np.take_along_axis(bank, idx[None], 0)[0]
    y = y / (y.std() + 1e-6)
    return y * m


def features(path, ref):
    x, m = ridge_image(path, ref)
    return gabor_enhance(x, m), m


def _rot(img, ang, center):
    M = cv2.getRotationMatrix2D(center, ang, 1.0)
    return cv2.warpAffine(img, M, (img.shape[1], img.shape[0]), flags=cv2.INTER_LINEAR)


def match(probe, pm, tmpl, tm):
    """Meilleure corrélation normalisée sur rotation + translation.

    La sonde (partie centrale) est cherchée dans le modèle, entouré d'une
    marge nulle. Retourne (score, angle, dx, dy). Le score est la corrélation
    pondérée par le recouvrement : un recouvrement faible ne peut pas donner
    un bon score.
    """
    c = (W / 2, H / 2)
    pad = MAXSHIFT
    big = cv2.copyMakeBorder(tmpl, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=0)
    bigm = cv2.copyMakeBorder(tm, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=0)
    best = (-1.0, 0, 0, 0)
    for ang in ANGLES:
        p = _rot(probe, float(ang), c)
        q = _rot(pm, float(ang), c) > 0.5
        qm = q.astype(np.float32)
        # le gabarit : la sonde entière avec son masque
        num = cv2.matchTemplate(big, p, cv2.TM_CCORR)             # Σ T·I
        n_t = float((p * p).sum()) + 1e-6
        i2 = cv2.matchTemplate(big * big, qm, cv2.TM_CCORR)       # Σ I² sur masque sonde
        den = np.sqrt(n_t * i2 + 1e-6)
        ncc = num / den
        ov = cv2.matchTemplate(bigm, qm, cv2.TM_CCORR) / (qm.sum() + 1e-6)
        s = ncc * np.clip(ov / 0.5, 0, 1)                         # pénalise un faible recouvrement
        _, mx, _, loc = cv2.minMaxLoc(s)
        if mx > best[0]:
            best = (float(mx), int(ang), loc[0] - pad, loc[1] - pad)
    return best


# ---------------------------------------------------------------- minuties
def minutiae(y, m):
    """Points de crêtes (terminaisons, bifurcations) : [x, y, angle, type]."""
    from skimage.morphology import skeletonize
    b = (y > 0) & (m > 0.5)
    b = cv2.morphologyEx(b.astype(np.uint8), cv2.MORPH_OPEN, np.ones((2, 2), np.uint8)) > 0
    sk = skeletonize(b).astype(np.uint8)
    k = np.ones((3, 3), np.float32)
    nb = cv2.filter2D(sk.astype(np.float32), -1, k, borderType=cv2.BORDER_CONSTANT) - sk
    inside = cv2.erode(m, np.ones((15, 15), np.uint8)) > 0.5
    out = []
    ys, xs = np.nonzero((sk > 0) & inside & ((nb == 1) | (nb >= 3)))
    for py, px in zip(ys, xs):
        t = 1 if nb[py, px] == 1 else 3
        # direction : vers les voisins sur 6 pas de squelette
        patch = sk[max(py - 6, 0):py + 7, max(px - 6, 0):px + 7]
        qy, qx = np.nonzero(patch)
        qy = qy + max(py - 6, 0) - py
        qx = qx + max(px - 6, 0) - px
        if len(qx) < 4:
            continue
        if t == 1:
            ang = np.arctan2(qy.mean(), qx.mean()) + np.pi      # pointe vers l'extérieur de la crête
        else:
            ang = np.arctan2(qy.mean(), qx.mean())
        out.append((px, py, ang, t))
    # retire les points trop proches (fausses minuties : ponts, pores)
    pts = np.array(out, np.float32).reshape(-1, 4)
    keep = []
    for i, p in enumerate(pts):
        d = np.hypot(pts[:, 0] - p[0], pts[:, 1] - p[1])
        if (d[np.arange(len(pts)) != i] < 6).any():
            continue
        keep.append(i)
    return pts[keep]


def match_min(A, B, dpos=9.0, dang=0.45):
    """Alignement par vote de Hough sur les paires de minuties ; renvoie (score, nb)."""
    if len(A) < 4 or len(B) < 4:
        return 0.0, 0
    votes = {}
    for i, a in enumerate(A):
        for j, b in enumerate(B):
            dth = (b[2] - a[2] + np.pi) % (2 * np.pi) - np.pi
            c, s = np.cos(dth), np.sin(dth)
            tx = b[0] - (c * a[0] - s * a[1])
            ty = b[1] - (s * a[0] + c * a[1])
            key = (int(round(dth / 0.2)), int(round(tx / 8)), int(round(ty / 8)))
            votes[key] = votes.get(key, 0) + 1
    top = sorted(votes, key=votes.get, reverse=True)[:6]
    best = 0
    for key in top:
        for dk in [(0, 0, 0)]:
            dth = key[0] * 0.2
            c, s = np.cos(dth), np.sin(dth)
            tx, ty = key[1] * 8, key[2] * 8
            # affiner : moyenne des translations des paires qui votent pour ce bin
            X = np.c_[c * A[:, 0] - s * A[:, 1] + tx, s * A[:, 0] + c * A[:, 1] + ty]
            used, n = set(), 0
            for i in range(len(A)):
                d = np.hypot(B[:, 0] - X[i, 0], B[:, 1] - X[i, 1])
                for j in np.argsort(d)[:2]:
                    da = abs((B[j, 2] - A[i, 2] - dth + np.pi) % (2 * np.pi) - np.pi)
                    if d[j] < dpos * 1.6 and da < dang and j not in used and A[i, 3] == B[j, 3]:
                        used.add(j)
                        n += 1
                        break
            best = max(best, n)
    return best / np.sqrt(len(A) * len(B)), best


# ------------------------------------------------------------------- base
def fuse(ncc, mscore):
    """Score fusionné : moyenne de la corrélation et du score de minuties."""
    return 0.5 * ncc + 0.5 * mscore


def compare(fa, ma, ta, tm, mna=None, mnb=None):
    """mna / mnb : minuties déjà extraites (sinon recalculées : lent)."""
    ncc = max(match(fa, ma, ta, tm)[0], 0.0)
    if mna is None:
        mna = minutiae(fa, ma)
    if mnb is None:
        mnb = minutiae(ta, tm)
    ms = match_min(mna, mnb)[0]
    return fuse(ncc, ms), ncc, ms


def need_ref():
    if not os.path.exists(ref_path()):
        sys.exit('pas de référence : gfreco.py ref CAPTURE_SANS_DOIGT.bin')
    return np.load(ref_path())


def put(name, k, f, m, mn):
    d = os.path.join(DB, name)
    os.makedirs(d, mode=0o700, exist_ok=True)
    p = os.path.join(d, f'{k}.npz')
    np.savez_compressed(p, f=f, m=m, mn=mn)
    os.chmod(p, 0o600)


def templates(name=None):
    out = []
    if not os.path.isdir(DB):
        return out
    for n in sorted(os.listdir(DB)):
        d = os.path.join(DB, n)
        if os.path.isdir(d) and (name is None or n == name):
            for f in sorted(os.listdir(d)):
                z = np.load(os.path.join(d, f))
                out.append((n, z['f'], z['m'], z['mn']))
    return out


def main():
    a = sys.argv[1:]
    if len(a) < 2:
        sys.exit(__doc__)
    cmd = a[0]
    if cmd == 'ref':
        os.makedirs(DB, mode=0o700, exist_ok=True)
        np.save(ref_path(), mean_frame(a[1]))
        os.chmod(ref_path(), 0o600)
        print('référence enregistrée')
    elif cmd == 'enroll':
        ref = need_ref()
        for k, p in enumerate(a[2:]):
            f, m = features(p, ref)
            n = len(templates(a[1]))
            mn = minutiae(f, m)
            put(a[1], n, f, m, mn)
            print(f'{p} -> modèle {n} de « {a[1]} » ({len(mn)} minuties)')
    elif cmd == 'verify':
        ref = need_ref()
        f, m = features(a[1], ref)
        mn = minutiae(f, m)
        best = ('', -1.0, 0, 0)
        for n, t, tm, tmn in templates(a[2] if len(a) > 2 else None):
            s, ncc, ms = compare(f, m, t, tm, mn, tmn)
            if s > best[1]:
                best = (n, s, ncc, ms)
        ok = best[1] >= THRESH
        print(f'{"RECONNU " + best[0] if ok else "REFUSÉ"}  score {best[1]:.3f} '
              f'(corrélation {best[2]:.3f}, minuties {best[3]:.3f}, seuil {THRESH})')
        sys.exit(0 if ok else 1)
    elif cmd == 'score':
        ref = need_ref()
        fa, ma = features(a[1], ref)
        fb, mb = features(a[2], ref)
        s, ncc, ms = compare(fa, ma, fb, mb, minutiae(fa, ma), minutiae(fb, mb))
        print(f'score {s:.3f} (corrélation {ncc:.3f}, minuties {ms:.3f})')
    elif cmd == 'eval':
        evaluate(a[1])
    else:
        sys.exit(__doc__)


def _cmp(a, b):
    return compare(a[0], a[1], b[0], b[1], a[2], b[2])[0]


def evaluate(root):
    """DOSSIER/<doigt>/*.bin + DOSSIER/ref.bin : scores genuine / imposteur, EER."""
    ref = mean_frame(os.path.join(root, 'ref.bin'))
    data = {}
    for d in sorted(os.listdir(root)):
        p = os.path.join(root, d)
        if os.path.isdir(p):
            data[d] = []
            for f in sorted(os.listdir(p)):
                if f.endswith('.bin'):
                    x, m = features(os.path.join(p, f), ref)
                    data[d].append((x, m, minutiae(x, m)))
    gen, imp = [], []
    names = list(data)
    for i, n in enumerate(names):
        for x in range(len(data[n])):
            for y in range(len(data[n])):
                if x < y:
                    gen.append(_cmp(data[n][x], data[n][y]))
        for m in names[i + 1:]:
            for fa in data[n]:
                for fb in data[m]:
                    imp.append(_cmp(fa, fb))
    if not gen or not imp:
        sys.exit('il faut au moins 2 captures par doigt et 2 doigts')
    gen, imp = np.array(gen), np.array(imp)
    ths = np.linspace(0, 1, 201)
    frr = np.array([(gen < t).mean() for t in ths])
    far = np.array([(imp >= t).mean() for t in ths])
    k = int(np.argmin(np.abs(frr - far)))
    print(f'paires : {len(gen)} authentiques, {len(imp)} imposteurs')
    print(f'authentiques : moy {gen.mean():.3f}  min {gen.min():.3f}  max {gen.max():.3f}')
    print(f'imposteurs   : moy {imp.mean():.3f}  min {imp.min():.3f}  max {imp.max():.3f}')
    print(f'EER ≈ {100 * (frr[k] + far[k]) / 2:.1f} % au seuil {ths[k]:.3f}')
    for t_far in (0.01, 0.001):
        ok = [t for t, f in zip(ths, far) if f <= t_far]
        if ok:
            t = ok[0]
            print(f'FAR ≤ {t_far:.1%} : seuil {t:.3f}, FRR {100 * (gen < t).mean():.1f} %')


if __name__ == '__main__':
    main()
