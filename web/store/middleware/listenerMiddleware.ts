import { createListenerMiddleware } from "@reduxjs/toolkit";
import { clearAuth } from "../slices/authSlice";
import { clearPermissions } from "../slices/permissionSlice";
import { clearTabs } from "../slices/tabsSlice";
import type { RootState, AppDispatch } from "../index";

export const listenerMiddleware = createListenerMiddleware();

const startAppListening = listenerMiddleware.startListening.withTypes<RootState, AppDispatch>();

// 当清除认证时，同时清除权限和标签页
startAppListening({
  actionCreator: clearAuth,
  effect: async (_, listenerApi) => {
    listenerApi.dispatch(clearPermissions());
    listenerApi.dispatch(clearTabs());
  },
});
