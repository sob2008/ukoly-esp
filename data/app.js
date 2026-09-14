// Úkolovník – veškerá logika appky (vanilla JS, bez build kroku, bez CDN závislostí).
// Appka vždy čte a zobrazuje data z localStorage (zdroj pravdy pro UI) a v pozadí
// se snaží synchronizovat s ESP32, ať běží přímo na zařízení nebo jako nainstalovaná PWA.

(() => {
  "use strict";

  // ============================================================
  // ÚLOŽIŠTĚ
  // ============================================================
  const STORAGE_KEYS = {
    tasks: "ukoly:tasks",
    categories: "ukoly:categories",
    lastSyncAt: "ukoly:lastSyncAt",
    serverUrl: "ukoly:serverUrl",
  };

  function loadArray(key) {
    try {
      const raw = localStorage.getItem(key);
      return raw ? JSON.parse(raw) : [];
    } catch (e) {
      console.error("Chyba cteni", key, e);
      return [];
    }
  }

  function saveArray(key, arr) {
    localStorage.setItem(key, JSON.stringify(arr));
  }

  let state = {
    tasks: loadArray(STORAGE_KEYS.tasks),
    categories: loadArray(STORAGE_KEYS.categories),
  };

  function persistTasks() {
    saveArray(STORAGE_KEYS.tasks, state.tasks);
  }

  function persistCategories() {
    saveArray(STORAGE_KEYS.categories, state.categories);
  }

  function nowSec() {
    return Math.floor(Date.now() / 1000);
  }

  function newId() {
    if (window.crypto && crypto.randomUUID) {
      return crypto.randomUUID();
    }
    return "id-" + Date.now() + "-" + Math.random().toString(16).slice(2);
  }

  // ============================================================
  // DOM ZKRATKY
  // ============================================================
  const $ = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

  // ============================================================
  // NAVIGACE
  // ============================================================
  const views = {
    tasks: $("#view-tasks"),
    categories: $("#view-categories"),
    "category-detail": $("#view-category-detail"),
    settings: $("#view-settings"),
  };
  const navButtons = $$(".nav-btn");

  function switchView(name) {
    Object.entries(views).forEach(([key, el]) => {
      el.classList.toggle("active", key === name);
    });
    navButtons.forEach((btn) => {
      btn.classList.toggle("active", btn.dataset.view === name);
    });
  }

  navButtons.forEach((btn) => {
    btn.addEventListener("click", () => {
      switchView(btn.dataset.view);
      if (btn.dataset.view === "tasks") renderTasks();
      if (btn.dataset.view === "categories") renderCategories();
      if (btn.dataset.view === "settings") renderSettings();
    });
  });

  // ============================================================
  // POMOCNÉ FUNKCE NAD DATY
  // ============================================================
  const PRIORITY_ORDER = { vysoka: 0, stredni: 1, nizka: 2 };
  const PRIORITY_LABEL = { vysoka: "Vysoká", stredni: "Střední", nizka: "Nízká" };

  function visibleTasks(filterFn) {
    return state.tasks
      .filter((t) => !t.deleted)
      .filter(filterFn || (() => true))
      .sort((a, b) => {
        if (a.done !== b.done) return a.done ? 1 : -1;
        const pa = PRIORITY_ORDER[a.priority] ?? 1;
        const pb = PRIORITY_ORDER[b.priority] ?? 1;
        if (pa !== pb) return pa - pb;
        return (a.deadline || "").localeCompare(b.deadline || "");
      });
  }

  function visibleCategories() {
    return state.categories.filter((c) => !c.deleted);
  }

  function categoryName(categoryId) {
    if (!categoryId) return "Nezařazeno";
    const cat = state.categories.find((c) => c.id === categoryId && !c.deleted);
    return cat ? cat.name : "Nezařazeno";
  }

  function todayStr() {
    const d = new Date();
    const y = d.getFullYear();
    const m = String(d.getMonth() + 1).padStart(2, "0");
    const day = String(d.getDate()).padStart(2, "0");
    return `${y}-${m}-${day}`;
  }

  function isOverdue(task) {
    return !!task.deadline && !task.done && task.deadline < todayStr();
  }

  // ============================================================
  // RENDER: seznam úkolů (jeden task-item)
  // ============================================================
  function taskItemHtml(task, opts = {}) {
    const showCategory = opts.showCategory !== false;
    const overdueClass = isOverdue(task) ? "overdue" : "";
    const deadlineHtml = task.deadline
      ? `<span class="deadline ${overdueClass}">${escapeHtml(task.deadline)}</span>`
      : "";
    const categoryHtml = showCategory
      ? `<span class="category-tag">${escapeHtml(categoryName(task.categoryId))}</span>`
      : "";

    return `
      <li class="task-item priority-${escapeAttr(task.priority)} ${task.done ? "done" : ""}" data-id="${escapeAttr(task.id)}">
        <input type="checkbox" class="task-checkbox" ${task.done ? "checked" : ""} data-action="toggle" data-id="${escapeAttr(task.id)}" />
        <div class="task-body">
          <div class="task-title">${escapeHtml(task.title)}</div>
          <div class="task-meta">
            ${categoryHtml}
            ${deadlineHtml}
          </div>
        </div>
        <button class="task-delete" data-action="delete" data-id="${escapeAttr(task.id)}" aria-label="Smazat">✕</button>
      </li>`;
  }

  function escapeHtml(str) {
    return String(str ?? "").replace(/[&<>"']/g, (c) => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#39;",
    }[c]));
  }
  function escapeAttr(str) {
    return escapeHtml(str);
  }

  function bindTaskListEvents(listEl) {
    listEl.addEventListener("click", (e) => {
      const delBtn = e.target.closest('[data-action="delete"]');
      if (delBtn) {
        deleteTask(delBtn.dataset.id);
        return;
      }
    });
    listEl.addEventListener("change", (e) => {
      const cb = e.target.closest('[data-action="toggle"]');
      if (cb) {
        toggleTaskDone(cb.dataset.id, cb.checked);
      }
    });
  }

  // ============================================================
  // SEKCE: ÚKOLY
  // ============================================================
  const taskListEl = $("#task-list");
  const taskListEmptyEl = $("#task-list-empty");
  bindTaskListEvents(taskListEl);

  function renderTasks() {
    const items = visibleTasks();
    taskListEl.innerHTML = items.map((t) => taskItemHtml(t)).join("");
    taskListEmptyEl.classList.toggle("hidden", items.length > 0);
    renderCategorySelect();
  }

  function renderCategorySelect() {
    const select = $("#task-category");
    const currentValue = select.value;
    const cats = visibleCategories();
    select.innerHTML =
      `<option value="">Nezařazeno</option>` +
      cats.map((c) => `<option value="${escapeAttr(c.id)}">${escapeHtml(c.name)}</option>`).join("") +
      `<option value="__new__">+ nová kategorie…</option>`;
    if ([...select.options].some((o) => o.value === currentValue)) {
      select.value = currentValue;
    }
  }

  $("#task-category").addEventListener("change", (e) => {
    const newCatField = $("#new-category-field");
    newCatField.classList.toggle("hidden", e.target.value !== "__new__");
  });

  $("#task-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const title = $("#task-title").value.trim();
    if (!title) return;

    const priority = $("#task-priority").value;
    const deadline = $("#task-deadline").value;
    const categorySelect = $("#task-category");
    let categoryId = categorySelect.value;

    if (categoryId === "__new__") {
      const newName = $("#task-new-category-name").value.trim();
      if (!newName) {
        alert("Zadej název nové kategorie.");
        return;
      }
      const cat = createCategory(newName);
      categoryId = cat.id;
    }

    createTask({ title, priority, deadline, categoryId });

    e.target.reset();
    $("#task-priority").value = "stredni";
    $("#new-category-field").classList.add("hidden");
    renderTasks();
  });

  // ============================================================
  // DATOVÉ OPERACE: ÚKOLY
  // ============================================================
  function createTask({ title, description = "", priority = "stredni", deadline = "", categoryId = "" }) {
    const task = {
      id: newId(),
      title,
      description,
      categoryId: categoryId || "",
      priority,
      deadline: deadline || "",
      done: false,
      updatedAt: nowSec(),
      deleted: false,
    };
    state.tasks.push(task);
    persistTasks();
    return task;
  }

  function toggleTaskDone(id, done) {
    const task = state.tasks.find((t) => t.id === id);
    if (!task) return;
    task.done = done;
    task.updatedAt = nowSec();
    persistTasks();
    renderTasks();
    if (currentCategoryId) renderCategoryDetail();
  }

  function deleteTask(id) {
    const task = state.tasks.find((t) => t.id === id);
    if (!task) return;
    task.deleted = true;
    task.updatedAt = nowSec();
    persistTasks();
    renderTasks();
    if (currentCategoryId) renderCategoryDetail();
    renderCategories();
  }

  function createCategory(name) {
    const cat = {
      id: newId(),
      name,
      updatedAt: nowSec(),
      deleted: false,
    };
    state.categories.push(cat);
    persistCategories();
    return cat;
  }

  // ============================================================
  // SEKCE: KATEGORIE (seznam)
  // ============================================================
  const categoryListEl = $("#category-list");
  const categoryListEmptyEl = $("#category-list-empty");

  function renderCategories() {
    const cats = visibleCategories();
    categoryListEl.innerHTML = cats
      .map((c) => {
        const openCount = state.tasks.filter(
          (t) => !t.deleted && !t.done && t.categoryId === c.id
        ).length;
        return `
          <li class="category-row" data-id="${escapeAttr(c.id)}">
            <span class="name">${escapeHtml(c.name)}</span>
            <span>
              <span class="count">${openCount}</span>
              <span class="arrow">→</span>
            </span>
          </li>`;
      })
      .join("");
    categoryListEmptyEl.classList.toggle("hidden", cats.length > 0);
  }

  categoryListEl.addEventListener("click", (e) => {
    const row = e.target.closest(".category-row");
    if (!row) return;
    openCategoryDetail(row.dataset.id);
  });

  $("#category-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const input = $("#new-category-name");
    const name = input.value.trim();
    if (!name) return;
    createCategory(name);
    input.value = "";
    renderCategories();
    renderCategorySelect();
  });

  // ============================================================
  // SEKCE: DETAIL KATEGORIE
  // ============================================================
  let currentCategoryId = null;
  const categoryDetailListEl = $("#category-detail-task-list");
  const categoryDetailEmptyEl = $("#category-detail-empty");
  bindTaskListEvents(categoryDetailListEl);

  function openCategoryDetail(categoryId) {
    currentCategoryId = categoryId;
    switchView("category-detail");
    renderCategoryDetail();
  }

  function renderCategoryDetail() {
    if (!currentCategoryId) return;
    $("#category-detail-title").textContent = categoryName(currentCategoryId);
    const items = visibleTasks((t) => t.categoryId === currentCategoryId);
    categoryDetailListEl.innerHTML = items.map((t) => taskItemHtml(t, { showCategory: false })).join("");
    categoryDetailEmptyEl.classList.toggle("hidden", items.length > 0);
  }

  $("#category-detail-back").addEventListener("click", () => {
    currentCategoryId = null;
    switchView("categories");
    renderCategories();
  });

  $("#category-task-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const title = $("#cat-task-title").value.trim();
    if (!title || !currentCategoryId) return;
    const priority = $("#cat-task-priority").value;
    const deadline = $("#cat-task-deadline").value;
    createTask({ title, priority, deadline, categoryId: currentCategoryId });
    e.target.reset();
    $("#cat-task-priority").value = "stredni";
    renderCategoryDetail();
    renderCategories();
  });

  // ============================================================
  // SEKCE: NASTAVENÍ
  // ============================================================
  function getServerUrl() {
    return localStorage.getItem(STORAGE_KEYS.serverUrl) || "";
  }

  function isRunningOnDevice() {
    // Appka běží přímo na ESP32, pokud není nastavená ruční adresa serveru –
    // relativní URL "/api/..." v takovém případě funguje sama o sobě.
    return !getServerUrl();
  }

  function renderSettings() {
    $("#server-url").value = getServerUrl();
    const lastSync = localStorage.getItem(STORAGE_KEYS.lastSyncAt);
    $("#last-sync-value").textContent = lastSync
      ? new Date(Number(lastSync)).toLocaleString("cs-CZ")
      : "nikdy";
    $("#sync-status-value").textContent = isRunningOnDevice()
      ? "běží přímo na ESP32 (automatický sync)"
      : "nainstalovaná appka (ruční / při návratu do sítě)";
  }

  $("#save-server-url").addEventListener("click", () => {
    const value = $("#server-url").value.trim().replace(/\/+$/, "");
    localStorage.setItem(STORAGE_KEYS.serverUrl, value);
    renderSettings();
  });

  $("#sync-now-btn").addEventListener("click", () => {
    runSync(true);
  });

  // ============================================================
  // SYNCHRONIZACE S ESP32
  // ============================================================
  async function attemptSync(baseUrl) {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 3000);
    try {
      const res = await fetch(`${baseUrl}/api/time`, { signal: controller.signal });
      clearTimeout(timeoutId);
      if (!res.ok) return false;
      await res.json();
      return true;
    } catch (e) {
      clearTimeout(timeoutId);
      return false;
    }
  }

  function mergeCollections(localArr, remoteArr, localKey, saveFn) {
    const localById = new Map(localArr.map((item) => [item.id, item]));
    const toPush = [];

    for (const remoteItem of remoteArr) {
      const localItem = localById.get(remoteItem.id);
      if (!localItem) {
        // server má záznam, který lokálně chybí -> přijmi ho
        localArr.push(remoteItem);
        localById.set(remoteItem.id, remoteItem);
      } else if ((remoteItem.updatedAt || 0) > (localItem.updatedAt || 0)) {
        // server má novější verzi -> přepiš lokální
        Object.assign(localItem, remoteItem);
      } else if ((localItem.updatedAt || 0) > (remoteItem.updatedAt || 0)) {
        // lokální verze je novější -> pošli na server
        toPush.push(localItem);
      }
    }

    // lokální záznamy, které server vůbec nezná -> taky pošli
    const remoteIds = new Set(remoteArr.map((r) => r.id));
    for (const localItem of localArr) {
      if (!remoteIds.has(localItem.id)) {
        toPush.push(localItem);
      }
    }

    saveFn(localArr);
    return toPush;
  }

  async function pushCategory(baseUrl, cat) {
    try {
      await fetch(`${baseUrl}/api/categories`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id: cat.id, name: cat.name }),
      });
    } catch (e) {
      /* tiché selhání, zkusí se pri pristim syncu */
    }
  }

  async function pushTask(baseUrl, task) {
    try {
      // zkus update; pokud úkol na serveru ještě neexistuje, endpoint ho stejně
      // najde podle id jen pokud existuje -> proto zkusíme rovnou "vytvořit / upravit"
      // pres /api/tasks/update, a pokud selže (404), zkusíme vytvoreni.
      const updateRes = await fetch(`${baseUrl}/api/tasks/update`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(task),
      });
      if (updateRes.status === 404) {
        await fetch(`${baseUrl}/api/tasks`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify(task),
        });
      }
    } catch (e) {
      /* tiché selhání, zkusí se pri pristim syncu */
    }
  }

  let syncInProgress = false;

  async function runSync(manual = false) {
    if (syncInProgress) return;
    const baseUrl = isRunningOnDevice() ? "" : getServerUrl();
    if (baseUrl === null || baseUrl === undefined) return;
    // baseUrl == "" je v pořádku (relativní URL na ESP32); pokud appka NENÍ na
    // zařízení a server URL není vyplněná, sync se přeskakuje.
    if (!isRunningOnDevice() && !getServerUrl()) {
      if (manual) alert("Nejdřív vyplň adresu ESP32 v Nastavení.");
      return;
    }

    syncInProgress = true;
    try {
      const reachable = await attemptSync(baseUrl);
      if (!reachable) {
        return;
      }

      const [catRes, taskRes] = await Promise.all([
        fetch(`${baseUrl}/api/categories`),
        fetch(`${baseUrl}/api/tasks`),
      ]);
      const remoteCategories = await catRes.json();
      const remoteTasks = await taskRes.json();

      const categoriesToPush = mergeCollections(
        state.categories,
        remoteCategories,
        "categories",
        persistCategories
      );
      const tasksToPush = mergeCollections(state.tasks, remoteTasks, "tasks", persistTasks);

      for (const cat of categoriesToPush) {
        await pushCategory(baseUrl, cat);
      }
      for (const task of tasksToPush) {
        await pushTask(baseUrl, task);
      }

      localStorage.setItem(STORAGE_KEYS.lastSyncAt, String(Date.now()));

      renderTasks();
      renderCategories();
      if (currentCategoryId) renderCategoryDetail();
      renderSettings();
    } finally {
      syncInProgress = false;
    }
  }

  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "visible") {
      runSync(false);
    }
  });

  // ============================================================
  // SERVICE WORKER
  // ============================================================
  if ("serviceWorker" in navigator) {
    window.addEventListener("load", () => {
      navigator.serviceWorker.register("/sw.js").catch((e) => {
        console.warn("Registrace service workeru selhala:", e);
      });
    });
  }

  // ============================================================
  // START
  // ============================================================
  renderTasks();
  renderCategories();
  renderSettings();
  runSync(false);
})();
