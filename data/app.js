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
    calendar: $("#view-calendar"),
    settings: $("#view-settings"),
  };
  const navButtons = $$(".nav-btn");
  const fabButton = $("#fab-add");
  let currentView = "tasks";

  function switchView(name) {
    currentView = name;
    Object.entries(views).forEach(([key, el]) => {
      el.classList.toggle("active", key === name);
    });
    navButtons.forEach((btn) => {
      btn.classList.toggle("active", btn.dataset.view === name);
    });
    updateFabVisibility();
  }

  navButtons.forEach((btn) => {
    btn.addEventListener("click", () => {
      switchView(btn.dataset.view);
      if (btn.dataset.view === "tasks") renderTasks();
      if (btn.dataset.view === "categories") renderCategories();
      if (btn.dataset.view === "calendar") renderCalendar();
      if (btn.dataset.view === "settings") renderSettings();
    });
  });

  function updateFabVisibility() {
    const visibleOn = ["tasks", "categories", "category-detail", "calendar"];
    fabButton.classList.toggle("hidden-fab", !visibleOn.includes(currentView));
  }

  fabButton.addEventListener("click", () => {
    if (currentView === "categories") {
      openCategoryModal();
      return;
    }
    if (currentView === "category-detail") {
      openTaskModal({ lockCategoryId: currentCategoryId });
      return;
    }
    if (currentView === "calendar") {
      openTaskModal({ deadline: calendarSelectedDate });
      return;
    }
    openTaskModal({});
  });

  // ============================================================
  // POMOCNÉ FUNKCE NAD DATY
  // ============================================================
  const PRIORITY_ORDER = { vysoka: 0, stredni: 1, nizka: 2 };

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

  function pad2(n) {
    return String(n).padStart(2, "0");
  }

  function dateToYMD(d) {
    return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())}`;
  }

  function todayStr() {
    return dateToYMD(new Date());
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
    const descriptionHtml = task.description
      ? `<div class="task-description">${escapeHtml(task.description)}</div>`
      : "";

    return `
      <li class="task-item priority-${escapeAttr(task.priority)} ${task.done ? "done" : ""}" data-id="${escapeAttr(task.id)}">
        <input type="checkbox" class="task-checkbox" ${task.done ? "checked" : ""} data-action="toggle" data-id="${escapeAttr(task.id)}" />
        <div class="task-body">
          <div class="task-title">${escapeHtml(task.title)}</div>
          ${descriptionHtml}
          <div class="task-meta">
            ${categoryHtml}
            ${deadlineHtml}
          </div>
        </div>
        <button class="task-delete" data-action="delete" data-id="${escapeAttr(task.id)}" aria-label="Smazat">
          <svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7h16M9 7V5a2 2 0 0 1 2-2h2a2 2 0 0 1 2 2v2m2 0-1 13a2 2 0 0 1-2 2H9a2 2 0 0 1-2-2L6 7"/></svg>
        </button>
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

  // ============================================================
  // MODAL: PŘIDAT ÚKOL
  // ============================================================
  const taskModalOverlay = $("#task-modal-overlay");
  const taskForm = $("#task-form");
  const taskCategoryField = $("#task-category-field");
  let taskModalLockCategoryId = null;

  function openTaskModal({ lockCategoryId = null, deadline = "" } = {}) {
    taskModalLockCategoryId = lockCategoryId;
    taskForm.reset();
    $("#task-priority").value = "stredni";
    $("#new-category-field").classList.add("hidden");
    if (deadline) {
      $("#task-deadline").value = deadline;
    }
    if (lockCategoryId) {
      taskCategoryField.classList.add("hidden");
    } else {
      taskCategoryField.classList.remove("hidden");
      renderCategorySelect();
    }
    taskModalOverlay.hidden = false;
    setTimeout(() => $("#task-title").focus(), 50);
  }

  function closeTaskModal() {
    taskModalOverlay.hidden = true;
    taskModalLockCategoryId = null;
  }

  $("#task-modal-close").addEventListener("click", closeTaskModal);
  taskModalOverlay.addEventListener("click", (e) => {
    if (e.target === taskModalOverlay) closeTaskModal();
  });

  taskForm.addEventListener("submit", (e) => {
    e.preventDefault();
    const title = $("#task-title").value.trim();
    if (!title) return;

    const description = $("#task-description").value.trim();
    const priority = $("#task-priority").value;
    const deadline = $("#task-deadline").value;

    let categoryId = taskModalLockCategoryId || "";
    if (!taskModalLockCategoryId) {
      const categorySelect = $("#task-category");
      categoryId = categorySelect.value;
      if (categoryId === "__new__") {
        const newName = $("#task-new-category-name").value.trim();
        if (!newName) {
          alert("Zadej název nové kategorie.");
          return;
        }
        const cat = createCategory(newName);
        categoryId = cat.id;
      }
    }

    createTask({ title, description, priority, deadline, categoryId });
    closeTaskModal();

    renderTasks();
    if (currentView === "category-detail") renderCategoryDetail();
    if (currentView === "calendar") renderCalendar();
    renderCategories();
  });

  // ============================================================
  // MODAL: NOVÁ KATEGORIE
  // ============================================================
  const categoryModalOverlay = $("#category-modal-overlay");
  const categoryForm = $("#category-form");

  function openCategoryModal() {
    categoryForm.reset();
    categoryModalOverlay.hidden = false;
    setTimeout(() => $("#new-category-name").focus(), 50);
  }

  function closeCategoryModal() {
    categoryModalOverlay.hidden = true;
  }

  $("#category-modal-close").addEventListener("click", closeCategoryModal);
  categoryModalOverlay.addEventListener("click", (e) => {
    if (e.target === categoryModalOverlay) closeCategoryModal();
  });

  categoryForm.addEventListener("submit", (e) => {
    e.preventDefault();
    const input = $("#new-category-name");
    const name = input.value.trim();
    if (!name) return;
    createCategory(name);
    closeCategoryModal();
    renderCategories();
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
    if (currentView === "calendar") renderCalendar();
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
    if (currentView === "calendar") renderCalendar();
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

  function deleteCategory(id) {
    const cat = state.categories.find((c) => c.id === id);
    if (!cat) return;
    cat.deleted = true;
    cat.updatedAt = nowSec();
    persistCategories();
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
            <button class="category-main" data-action="open" data-id="${escapeAttr(c.id)}">
              <span class="name">${escapeHtml(c.name)}</span>
              <span class="count">${openCount}</span>
            </button>
            <svg class="icon chevron" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 6l6 6-6 6"/></svg>
          </li>`;
      })
      .join("");
    categoryListEmptyEl.classList.toggle("hidden", cats.length > 0);
  }

  categoryListEl.addEventListener("click", (e) => {
    const openBtn = e.target.closest('[data-action="open"]');
    if (openBtn) {
      openCategoryDetail(openBtn.dataset.id);
    }
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

  $("#category-detail-delete").addEventListener("click", () => {
    if (!currentCategoryId) return;
    const name = categoryName(currentCategoryId);
    const taskCount = state.tasks.filter((t) => !t.deleted && t.categoryId === currentCategoryId).length;
    const extra = taskCount > 0 ? `\n\n${taskCount} úkol(ů) v ní zůstane, jen se přeřadí do „Nezařazeno“.` : "";
    if (!confirm(`Opravdu smazat kategorii „${name}“?${extra}`)) return;
    deleteCategory(currentCategoryId);
    currentCategoryId = null;
    switchView("categories");
    renderCategories();
    renderTasks();
  });

  // ============================================================
  // SEKCE: KALENDÁŘ
  // ============================================================
  const MONTH_NAMES = [
    "leden", "únor", "březen", "duben", "květen", "červen",
    "červenec", "srpen", "září", "říjen", "listopad", "prosinec",
  ];

  let calendarViewDate = new Date();
  calendarViewDate.setDate(1);
  let calendarSelectedDate = todayStr();

  const calendarGridEl = $("#calendar-grid");
  const calendarDayListEl = $("#calendar-day-task-list");
  const calendarDayEmptyEl = $("#calendar-day-empty");
  bindTaskListEvents(calendarDayListEl);

  function tasksByDeadline(dateStr) {
    return visibleTasks((t) => t.deadline === dateStr);
  }

  function renderCalendar() {
    const year = calendarViewDate.getFullYear();
    const month = calendarViewDate.getMonth();
    $("#cal-month-label").textContent = `${MONTH_NAMES[month]} ${year}`;

    const firstOfMonth = new Date(year, month, 1);
    // pondeli = 0 ... nedele = 6
    const firstWeekday = (firstOfMonth.getDay() + 6) % 7;
    const daysInMonth = new Date(year, month + 1, 0).getDate();
    const today = todayStr();

    let html = "";
    for (let i = 0; i < firstWeekday; i++) {
      html += `<div class="calendar-day empty"></div>`;
    }
    for (let day = 1; day <= daysInMonth; day++) {
      const dateStr = `${year}-${pad2(month + 1)}-${pad2(day)}`;
      const dayTasks = tasksByDeadline(dateStr);
      let dotClass = "";
      if (dayTasks.some((t) => t.priority === "vysoka")) dotClass = "dot-vysoka";
      else if (dayTasks.some((t) => t.priority === "stredni")) dotClass = "dot-stredni";
      else if (dayTasks.length > 0) dotClass = "dot-nizka";

      const classes = ["calendar-day"];
      if (dateStr === today) classes.push("today");
      if (dateStr === calendarSelectedDate) classes.push("selected");

      const dotHtml = dayTasks.length > 0 ? `<span class="cal-dot ${dotClass}"></span>` : "";
      html += `<button type="button" class="${classes.join(" ")}" data-date="${dateStr}">${day}${dotHtml}</button>`;
    }
    calendarGridEl.innerHTML = html;
    renderCalendarDayPanel();
  }

  function renderCalendarDayPanel() {
    const d = new Date(calendarSelectedDate + "T00:00:00");
    const isToday = calendarSelectedDate === todayStr();
    $("#calendar-day-title").textContent = isToday
      ? "Dnes"
      : d.toLocaleDateString("cs-CZ", { weekday: "long", day: "numeric", month: "long" });

    const items = tasksByDeadline(calendarSelectedDate);
    calendarDayListEl.innerHTML = items.map((t) => taskItemHtml(t)).join("");
    calendarDayEmptyEl.classList.toggle("hidden", items.length > 0);
  }

  calendarGridEl.addEventListener("click", (e) => {
    const dayBtn = e.target.closest(".calendar-day[data-date]");
    if (!dayBtn) return;
    calendarSelectedDate = dayBtn.dataset.date;
    renderCalendar();
  });

  $("#cal-prev").addEventListener("click", () => {
    calendarViewDate.setMonth(calendarViewDate.getMonth() - 1);
    renderCalendar();
  });
  $("#cal-next").addEventListener("click", () => {
    calendarViewDate.setMonth(calendarViewDate.getMonth() + 1);
    renderCalendar();
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
  // INDIKÁTOR SYNCHRONIZACE (vpravo nahoře)
  // ============================================================
  const syncDotEl = $("#sync-dot");
  const syncIndicatorEl = $("#sync-indicator");

  function computeSyncStatus() {
    if (isRunningOnDevice()) {
      return { color: "green", label: "Přímo na zařízení" };
    }
    const raw = localStorage.getItem(STORAGE_KEYS.lastSyncAt);
    if (!raw) {
      return { color: "red", label: "Nikdy nesynchronizováno" };
    }
    const ageMs = Date.now() - Number(raw);
    const HOUR = 60 * 60 * 1000;
    const DAY = 24 * HOUR;
    const lastStr = new Date(Number(raw)).toLocaleString("cs-CZ");
    if (ageMs > DAY) {
      return { color: "red", label: `Nesynchronizováno přes den (naposledy ${lastStr})` };
    }
    if (ageMs > HOUR) {
      return { color: "yellow", label: `Synchronizováno ${lastStr}` };
    }
    return { color: "green", label: `Synchronizováno ${lastStr}` };
  }

  function renderSyncIndicator() {
    const status = computeSyncStatus();
    syncDotEl.className = `sync-dot sync-dot--${status.color}`;
    syncIndicatorEl.title = status.label;
  }

  syncIndicatorEl.addEventListener("click", () => {
    switchView("settings");
    renderSettings();
  });

  setInterval(renderSyncIndicator, 60000);

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
      if (cat.deleted) {
        await fetch(`${baseUrl}/api/categories/delete?id=${encodeURIComponent(cat.id)}`, {
          method: "POST",
        });
        return;
      }
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
      if (currentView === "calendar") renderCalendar();
      renderSettings();
      renderSyncIndicator();
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
  updateFabVisibility();
  renderTasks();
  renderCategories();
  renderCalendar();
  renderSettings();
  renderSyncIndicator();
  runSync(false);
})();
