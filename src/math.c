#include "vista.h"

m4 m4identity(void)
{
    m4 r = {{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    }};
    return r;
}

m4 m4mul(m4 a, m4 b)
{
    m4 r;
    for (int c = 0; c < 4; c++) {
        for (int i = 0; i < 4; i++) {
            r.m[c * 4 + i] =
                a.m[0 * 4 + i] * b.m[c * 4 + 0] +
                a.m[1 * 4 + i] * b.m[c * 4 + 1] +
                a.m[2 * 4 + i] * b.m[c * 4 + 2] +
                a.m[3 * 4 + i] * b.m[c * 4 + 3];
        }
    }
    return r;
}

m4 m4perspective(float fovy, float aspect, float znear, float zfar)
{
    float f = 1.0f / tanf(fovy * 0.5f);
    m4 r = {{0}};
    r.m[0] = f / aspect;
    r.m[5] = -f;
    r.m[10] = zfar / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (znear * zfar) / (znear - zfar);
    return r;
}

m4 m4frustum_xr(float angleLeft, float angleRight, float angleUp, float angleDown, float znear, float zfar)
{
    float tl = tanf(angleLeft);
    float tr = tanf(angleRight);
    float tu = tanf(angleUp);
    float td = tanf(angleDown);
    float w = tr - tl;
    float h = td - tu;
    m4 r = {{0}};
    r.m[0] = 2.0f / w;
    r.m[5] = 2.0f / h;
    r.m[8] = (tr + tl) / w;
    r.m[9] = (td + tu) / h;
    r.m[10] = zfar / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (znear * zfar) / (znear - zfar);
    return r;
}

m4 m4look(v3 eye, v3 center, v3 up)
{
    v3 f = v3norm(v3sub(center, eye));
    v3 s = v3norm(v3cross(f, up));
    v3 u = v3cross(s, f);
    m4 r = {{0}};
    r.m[0] = s.x;
    r.m[1] = u.x;
    r.m[2] = -f.x;
    r.m[4] = s.y;
    r.m[5] = u.y;
    r.m[6] = -f.y;
    r.m[8] = s.z;
    r.m[9] = u.z;
    r.m[10] = -f.z;
    r.m[12] = -v3dot(s, eye);
    r.m[13] = -v3dot(u, eye);
    r.m[14] = v3dot(f, eye);
    r.m[15] = 1.0f;
    return r;
}

m4 m4from_rt(q4 rot, v3 pos)
{
    float x = rot.x, y = rot.y, z = rot.z, w = rot.w;
    m4 r = {{0}};
    r.m[0] = 1.0f - 2.0f * (y * y + z * z);
    r.m[1] = 2.0f * (x * y + w * z);
    r.m[2] = 2.0f * (x * z - w * y);
    r.m[4] = 2.0f * (x * y - w * z);
    r.m[5] = 1.0f - 2.0f * (x * x + z * z);
    r.m[6] = 2.0f * (y * z + w * x);
    r.m[8] = 2.0f * (x * z + w * y);
    r.m[9] = 2.0f * (y * z - w * x);
    r.m[10] = 1.0f - 2.0f * (x * x + y * y);
    r.m[12] = pos.x;
    r.m[13] = pos.y;
    r.m[14] = pos.z;
    r.m[15] = 1.0f;
    return r;
}

m4 m4inverse_rt(m4 a)
{
    m4 r = {{0}};
    r.m[0] = a.m[0];
    r.m[1] = a.m[4];
    r.m[2] = a.m[8];
    r.m[4] = a.m[1];
    r.m[5] = a.m[5];
    r.m[6] = a.m[9];
    r.m[8] = a.m[2];
    r.m[9] = a.m[6];
    r.m[10] = a.m[10];
    r.m[12] = -(a.m[0] * a.m[12] + a.m[1] * a.m[13] + a.m[2] * a.m[14]);
    r.m[13] = -(a.m[4] * a.m[12] + a.m[5] * a.m[13] + a.m[6] * a.m[14]);
    r.m[14] = -(a.m[8] * a.m[12] + a.m[9] * a.m[13] + a.m[10] * a.m[14]);
    r.m[15] = 1.0f;
    return r;
}

v3 q4rotate(q4 q, v3 v)
{
    v3 u = (v3){q.x, q.y, q.z};
    v3 t = v3scale(v3cross(u, v), 2.0f);
    return v3add(v3add(v, v3scale(t, q.w)), v3cross(u, t));
}
