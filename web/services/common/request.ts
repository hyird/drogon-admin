/**
 * HTTP 请求基础配置
 * 从原 api/request.ts 迁移，作为 Service 层的底层依赖
 */

import axios, {
  type AxiosError,
  type AxiosInstance,
  type AxiosRequestConfig,
  type InternalAxiosRequestConfig,
} from "axios";
import { store } from "@/store";
import { clearAuth, refreshAccessToken } from "@/store/slices/authSlice";

/** 经过响应拦截器处理后的请求接口 - 直接返回数据而非 AxiosResponse */
export interface RequestConfig extends AxiosRequestConfig {
  authToken?: string;
  skipAuthRefresh?: boolean;
}

interface RequestInstance extends AxiosInstance {
  get<T = unknown>(url: string, config?: RequestConfig): Promise<T>;
  post<T = unknown>(url: string, data?: unknown, config?: RequestConfig): Promise<T>;
  put<T = unknown>(url: string, data?: unknown, config?: RequestConfig): Promise<T>;
  delete<T = unknown>(url: string, config?: RequestConfig): Promise<T>;
  patch<T = unknown>(url: string, data?: unknown, config?: RequestConfig): Promise<T>;
}

const request = axios.create({
  baseURL: "/",
  timeout: 30000,
}) as RequestInstance;

// 请求拦截器
request.interceptors.request.use(
  (config: InternalAxiosRequestConfig) => {
    const requestConfig = config as InternalAxiosRequestConfig & RequestConfig;
    const state = store.getState();
    const token = requestConfig.authToken ?? state.auth.token;
    if (token && config.headers) {
      config.headers.Authorization = `Bearer ${token}`;
    }
    return config;
  },
  (error) => Promise.reject(error)
);

type RefreshSubscriber = {
  resolve: (token: string) => void;
  reject: (reason?: unknown) => void;
};

// Token 刷新状态
let isRefreshing = false;
let refreshSubscribers: RefreshSubscriber[] = [];

function subscribeTokenRefresh(
  resolve: (token: string) => void,
  reject: (reason?: unknown) => void
) {
  refreshSubscribers.push({ resolve, reject });
}

function resolveTokenRefresh(token: string) {
  const subscribers = refreshSubscribers;
  refreshSubscribers = [];
  subscribers.forEach(({ resolve }) => resolve(token));
}

function rejectTokenRefresh(reason: unknown) {
  const subscribers = refreshSubscribers;
  refreshSubscribers = [];
  subscribers.forEach(({ reject }) => reject(reason));
}

function redirectToLogin() {
  if (typeof window !== "undefined") {
    // HashRouter 需要通过 hash 跳转，直接改 pathname 会绕过前端路由
    window.location.hash = "#/login";
  }
}

// 响应拦截器
request.interceptors.response.use(
  (response) => {
    const data = response.data;
    if (data.code !== undefined && data.code !== 0) {
      return Promise.reject(new Error(data.message || "请求失败"));
    }
    return data.data !== undefined ? data.data : data;
  },
  async (error: AxiosError<{ message?: string }>) => {
    const originalRequest = error.config as InternalAxiosRequestConfig &
      RequestConfig & { _retry?: boolean };

    // 401 处理：尝试刷新 token
    if (
      error.response?.status === 401 &&
      originalRequest &&
      !originalRequest._retry &&
      !originalRequest.skipAuthRefresh
    ) {
      if (isRefreshing) {
        return new Promise((resolve, reject) => {
          subscribeTokenRefresh((token: string) => {
            if (originalRequest.headers) {
              originalRequest.headers.Authorization = `Bearer ${token}`;
            }
            resolve(request(originalRequest));
          }, reject);
        });
      }

      originalRequest._retry = true;
      isRefreshing = true;

      try {
        const resultAction = await store.dispatch(refreshAccessToken());
        if (refreshAccessToken.fulfilled.match(resultAction)) {
          const newToken = resultAction.payload.token;
          resolveTokenRefresh(newToken);
          if (originalRequest.headers) {
            originalRequest.headers.Authorization = `Bearer ${newToken}`;
          }
          return request(originalRequest);
        }

        const message = resultAction.payload ?? "登录已过期，请重新登录";
        const refreshError = new Error(message);
        rejectTokenRefresh(refreshError);
        store.dispatch(clearAuth());
        redirectToLogin();
        return Promise.reject(refreshError);
      } catch (refreshError) {
        const errorToThrow =
          refreshError instanceof Error ? refreshError : new Error("刷新令牌失败");
        rejectTokenRefresh(errorToThrow);
        store.dispatch(clearAuth());
        redirectToLogin();
        return Promise.reject(errorToThrow);
      } finally {
        isRefreshing = false;
      }
    }

    const message = error.response?.data?.message || error.message || "网络错误";
    return Promise.reject(new Error(message));
  }
);

export default request;
