import { defineConfig } from "vite";
import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import path from "node:path";
import { fileURLToPath } from "node:url";
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const vendorChunkRules: Array<[RegExp, string]> = [
  [/node_modules[\\/]+@tanstack[\\/]/, "vendor-query"],
  [/node_modules[\\/]+ahooks[\\/]/, "vendor-ahooks"],
  [/node_modules[\\/]+@ant-design[\\/]/, "vendor-antd"],
  [/node_modules[\\/]+antd[\\/]/, "vendor-antd"],
  [/node_modules[\\/]+framer-motion[\\/]/, "vendor-motion"],
  [/node_modules[\\/]+@reduxjs[\\/]+toolkit[\\/]/, "vendor-state"],
  [/node_modules[\\/]+react-redux[\\/]/, "vendor-state"],
  [/node_modules[\\/]+redux-persist[\\/]/, "vendor-state"],
  [/node_modules[\\/]+react-router-dom[\\/]/, "vendor-router"],
  [/node_modules[\\/]+axios[\\/]/, "vendor-http"],
  [/node_modules[\\/]+react-dom[\\/]/, "vendor-react"],
  [/node_modules[\\/]+react[\\/]/, "vendor-react"],
];

export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: {
    port: 5173,
    proxy: {
      "/api": {
        target: "http://127.0.0.1:3000",
        changeOrigin: true,
      },
    },
  },
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "web"),
    },
  },
  build: {
    outDir: "dist/web",
    emptyOutDir: true,
    chunkSizeWarningLimit: 9999,
    rollupOptions: {
      output: {
        entryFileNames: "assets/[name]-[hash].js",
        chunkFileNames: "assets/[name]-[hash].js",
        assetFileNames: "assets/[name]-[hash][extname]",
        manualChunks(id) {
          if (!id.includes("node_modules")) {
            return;
          }

          for (const [pattern, chunkName] of vendorChunkRules) {
            if (pattern.test(id)) {
              return chunkName;
            }
          }
        },
      },
    },
  },
});
