package br.com.camxp.tv;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

/** Pure policies, shared by UI and host-side tests. No network or Android calls. */
public final class Model {
    public static final int MAX = 16, SIDE = 0, STACK = 1, GRID = 2;
    public static int[] channels(String value) {
        if (value.trim().isEmpty()) return new int[0];
        String[] parts = value.split(",", -1);
        if (parts.length > MAX) throw new IllegalArgumentException("Máximo de 16 câmeras");
        int[] result = new int[parts.length];
        HashSet<Integer> seen = new HashSet<>();
        for (int i = 0; i < parts.length; i++) {
            String part = parts[i].trim();
            if (!part.matches("[0-9]{1,2}")) throw new IllegalArgumentException("Canal inválido");
            int n = Integer.parseInt(part);
            if (n < 1 || n > MAX || !seen.add(n)) throw new IllegalArgumentException("Canal inválido ou repetido");
            result[i] = n;
        }
        return result;
    }
    public static boolean ipv4(String host) {
        String[] p = host.split("\\.", -1);
        if (p.length != 4) return false;
        for (String s : p) {
            if (!s.matches("[0-9]{1,3}")) return false;
            if (Integer.parseInt(s) > 255) return false;
        }
        return true;
    }
    public static int[] grid(int count, int mode, int width, int height, int header) {
        if (count <= 0) return new int[]{1, 1};
        if (mode == SIDE) return new int[]{count, 1};
        if (mode == STACK) return new int[]{1, count};
        int best = -1, empty = MAX, bc = 1, br = count;
        for (int c = 1; c <= count; c++) {
            int r = (count + c - 1) / c;
            int scale = Math.min(Math.max(1, width / c - 2) * 9, Math.max(1, height / r - header - 2) * 16);
            if (scale > best || (scale == best && c * r - count < empty)) {
                best = scale; empty = c * r - count; bc = c; br = r;
            }
        }
        return new int[]{bc, br};
    }
    public static long retryDelay(int attempts) {
        return attempts == 0 ? 60000 : attempts == 1 ? 120000 : attempts == 2 ? 240000 : 300000;
    }
    public static boolean due(long now, long due, long last, boolean hasLast) {
        return now >= due && (!hasLast || now - last >= 3000);
    }
    public static final class Watchdog {
        private long start, progress, stable;
        private int frames;
        private boolean seen;
        public boolean healthy;
        public Watchdog(long now) { start = progress = now; stable = -1; }
        /** null means healthy/waiting; otherwise a reason for recovery. */
        public String sample(long now, int state, boolean stats, int pictures, boolean video) {
            if (state == 5 || state == 6 || state == 7) return "Conexão encerrada ou com erro";
            if (stats && pictures > 0 && pictures != frames) {
                frames = pictures; progress = now; seen = true;
                if (stable < 0) stable = now;
                if (now - stable >= 15000) healthy = true;
            }
            if (!stats && state == 3 && video) { start = progress = now; return null; }
            if (!seen && now - start >= 45000) return "Sem primeiro quadro por 45 s";
            if (seen && now - progress >= 30000) return "Sem novos quadros por 30 s";
            return null;
        }
    }
    private Model() {}
}
