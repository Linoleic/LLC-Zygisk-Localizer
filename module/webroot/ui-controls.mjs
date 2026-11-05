const reducedMotion = () => matchMedia('(prefers-reduced-motion: reduce)').matches;

// Capture the current rendered height before cancelling: reversing mid-animation
// must continue from the visible frame instead of jumping to either endpoint.
function heightController(element, measure, finish) {
  let animation = null, desired = null, targetHeight = 0;
  const set = (open, animate = true, refresh = false) => {
    if (desired === open && !refresh) return;
    const from = element.getBoundingClientRect().height;
    desired = open;
    animation?.cancel(); animation = null;
    element.hidden = false;
    const target = open ? measure() : 0;
    targetHeight = target;
    element.style.height = `${target}px`; element.style.overflow = 'hidden';
    const complete = () => {
      animation = null;
      element.style.height = open ? 'auto' : '0px';
      element.style.overflow = open ? 'visible' : 'hidden';
      element.dataset.animating = 'false'; finish(open);
    };
    if (!animate || reducedMotion() || Math.abs(from - target) < 1) { complete(); return; }
    element.dataset.animating = 'true';
    const current = element.animate([{height: `${from}px`}, {height: `${target}px`}], {duration: 220, easing: 'cubic-bezier(0.2,0,0,1)'});
    animation = current;
    current.onfinish = () => { if (animation === current) complete(); };
  };
  return {set, refresh() { if (desired && animation && Math.abs(measure() - targetHeight) > 1) set(true, true, true); }, get desired() { return desired; }};
}

export function setupDisclosures(root = document) {
  const controls = new WeakMap();
  for (const details of root.querySelectorAll('details')) {
    const summary = details.querySelector(':scope > summary'); if (!summary) continue;
    const content = document.createElement('div'), body = document.createElement('div');
    content.className = 'disclosure-content'; body.className = 'disclosure-body';
    while (summary.nextSibling) body.append(summary.nextSibling);
    content.append(body); details.append(content);
    const height = heightController(content, () => body.getBoundingClientRect().height, open => { details.open = open; });
    const set = (open, animate = true) => {
      if (open) details.open = true;
      content.inert = !open;
      details.dataset.expanded = String(open); summary.setAttribute('aria-expanded', String(open));
      height.set(open, animate);
    };
    set(details.open, false);
    summary.addEventListener('click', event => { event.preventDefault(); set(!height.desired); });
    // Preserve native/programmatic .open semantics used by navigation and tools.
    details.addEventListener('toggle', () => {
      if (content.dataset.animating === 'true' && details.open) return;
      if (details.open !== height.desired) set(details.open, false);
    });
    controls.set(details, set);
    if (typeof ResizeObserver !== 'undefined') new ResizeObserver(() => height.refresh()).observe(body);
  }
  return {open(details) { controls.get(details)?.(true); }};
}

const regions = new WeakMap();
export function showRegion(element, open) {
  element.inert = !open;
  let control = regions.get(element);
  if (!control) {
    const body = document.createElement('span'); body.className = 'region-body';
    while (element.firstChild) body.append(element.firstChild);
    element.append(body); element.classList.remove('collapsed');
    control = heightController(element, () => body.getBoundingClientRect().height, visible => { element.hidden = !visible; });
    regions.set(element, control); control.set(open, false); return;
  }
  control.set(open);
}

export function setupSelects(root = document) {
  const controllers = [];
  let active = null;
  for (const select of root.querySelectorAll('select.select-field')) {
    const fieldName = select.labels?.[0]?.querySelector('span')?.textContent?.trim() || select.labels?.[0]?.textContent?.trim() || '选择选项';
    const trigger = document.createElement('button'); trigger.type = 'button'; trigger.className = 'select-trigger'; trigger.id = `${select.id}-trigger`;
    trigger.setAttribute('role', 'combobox'); trigger.setAttribute('aria-haspopup', 'listbox'); trigger.setAttribute('aria-expanded', 'false');
    const label = document.createElement('span'); label.className = 'select-value'; trigger.append(label);
    const menu = document.createElement('div'); menu.className = 'select-menu'; menu.id = `${select.id}-menu`; menu.hidden = true;
    menu.setAttribute('role', 'listbox'); menu.setAttribute('aria-label', fieldName); trigger.setAttribute('aria-controls', menu.id);
    select.before(trigger); document.body.append(menu);
    for (const parentLabel of Array.from(select.labels ?? [])) parentLabel.htmlFor = trigger.id;
    select.classList.add('select-native'); select.tabIndex = -1; select.setAttribute('aria-hidden', 'true');
    let open = false, animation = null, signature = '', options = [], current = -1, search = '', searchAt = 0;
    const enabled = index => options[index] && !options[index].disabled && !options[index].hidden;
    const position = () => {
      if (menu.hidden) return;
      const box = trigger.getBoundingClientRect(), below = innerHeight - box.bottom - 12, above = box.top - 12;
      const up = below < 180 && above > below;
      menu.style.width = `${Math.min(box.width, innerWidth - 16)}px`;
      menu.style.left = `${Math.max(8, Math.min(box.left, innerWidth - box.width - 8))}px`;
      menu.style.maxHeight = `${Math.max(48, Math.min(320, up ? above : below))}px`;
      menu.style.top = up ? 'auto' : `${box.bottom + 6}px`;
      menu.style.bottom = up ? `${innerHeight - box.top + 6}px` : 'auto';
    };
    const highlight = index => {
      current = index;
      for (let i = 0; i < menu.children.length; ++i) menu.children[i].classList.toggle('is-active', i === index);
      if (index >= 0) {
        trigger.setAttribute('aria-activedescendant', menu.children[index].id);
        menu.children[index].scrollIntoView({block: 'nearest'});
      }
    };
    const transition = visible => {
      const wasHidden = menu.hidden;
      const from = wasHidden ? {opacity: 0, transform: 'translateY(-6px)'} : {opacity: getComputedStyle(menu).opacity, transform: getComputedStyle(menu).transform};
      animation?.cancel(); animation = null;
      menu.hidden = false; position();
      if (reducedMotion()) { menu.hidden = !visible; return; }
      const end = visible ? {opacity: 1, transform: 'translateY(0)'} : {opacity: 0, transform: 'translateY(-6px)'};
      const next = menu.animate([from, end], {duration: visible ? 160 : 140, easing: 'cubic-bezier(0.2,0,0,1)'});
      animation = next;
      next.onfinish = () => { if (animation === next) { animation = null; menu.hidden = !open; } };
    };
    const close = () => {
      if (!open) return;
      open = false; trigger.setAttribute('aria-expanded', 'false'); trigger.removeAttribute('aria-activedescendant'); menu.style.pointerEvents = 'none';
      transition(false); if (active === controller) active = null;
    };
    const choose = index => {
      if (!enabled(index)) return;
      select.value = options[index].value;
      select.dispatchEvent(new Event('input', {bubbles: true})); select.dispatchEvent(new Event('change', {bubbles: true}));
      close(); trigger.focus();
    };
    const sync = () => {
      options = Array.from(select.options);
      const next = JSON.stringify(options.map(option => [option.value, option.textContent, option.disabled, option.hidden]));
      if (next !== signature) {
        signature = next; menu.replaceChildren();
        options.forEach((option, index) => {
          const item = document.createElement('div'); item.className = 'select-option'; item.id = `${select.id}-option-${index}`;
          item.textContent = option.textContent; item.setAttribute('role', 'option'); item.setAttribute('aria-disabled', String(option.disabled)); item.hidden = option.hidden;
          item.addEventListener('click', () => choose(index)); menu.append(item);
        });
      }
      const selected = options[select.selectedIndex]; label.textContent = selected?.textContent ?? '—';
      trigger.setAttribute('aria-label', `${fieldName}：${label.textContent}`); trigger.disabled = select.disabled;
      for (let i = 0; i < menu.children.length; ++i) menu.children[i].setAttribute('aria-selected', String(i === select.selectedIndex));
      if (select.disabled || !options.length) close(); else if (open) { position(); highlight(enabled(current) ? current : select.selectedIndex); }
    };
    const show = () => {
      if (select.disabled) return;
      active?.close(); open = true; active = controller; sync();
      trigger.setAttribute('aria-expanded', 'true'); menu.style.pointerEvents = 'auto'; transition(true);
      highlight(enabled(select.selectedIndex) ? select.selectedIndex : options.findIndex((_, i) => enabled(i)));
    };
    const controller = {trigger, menu, close, sync, position}; controllers.push(controller);
    trigger.addEventListener('click', () => { if (open) close(); else show(); });
    trigger.addEventListener('keydown', event => {
      if (event.key === 'Escape') { if (open) { event.preventDefault(); close(); } return; }
      if (event.key === 'Tab') { close(); return; }
      if (['Enter', ' '].includes(event.key)) { event.preventDefault(); if (open) choose(current); else show(); return; }
      if (['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
        event.preventDefault(); if (!open) show();
        const direction = event.key === 'ArrowUp' || event.key === 'End' ? -1 : 1;
        let index = event.key === 'Home' ? -1 : event.key === 'End' ? options.length : current;
        for (let n = 0; n < options.length; ++n) { index = (index + direction + options.length) % options.length; if (enabled(index)) { highlight(index); break; } }
      } else if (event.key.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
        search = Date.now() - searchAt > 600 ? event.key : search + event.key; searchAt = Date.now();
        if (!open) show();
        const index = options.findIndex((option, i) => enabled(i) && option.textContent.trim().toLocaleLowerCase().startsWith(search.toLocaleLowerCase()));
        if (index >= 0) highlight(index);
      }
    });
    select.addEventListener('change', sync); sync();
  }
  document.addEventListener('pointerdown', event => { if (active && !active.trigger.contains(event.target) && !active.menu.contains(event.target)) active.close(); });
  document.addEventListener('focusin', event => { if (active && !active.trigger.contains(event.target) && !active.menu.contains(event.target)) active.close(); });
  document.addEventListener('scroll', event => {
    if (!active || active.menu.contains(event.target)) return;
    const box = active.trigger.getBoundingClientRect();
    if (box.bottom <= 0 || box.top >= innerHeight) active.close(); else active.position();
  }, {capture: true});
  window.addEventListener('resize', () => active?.position());
  return {sync() { for (const controller of controllers) controller.sync(); }, close() { active?.close(); }};
}

export function setupTabs(onChange = () => {}) {
  const tabs = [...document.querySelectorAll('.tab-bar [role="tab"]')];
  let current = tabs.find(tab => tab.getAttribute('aria-selected') === 'true'), animation;
  const select = (name, focus = false) => {
    const target = tabs.find(tab => tab.id === `tab-${name}`);
    if (!target) return;
    if (target !== current) {
      onChange(); animation?.cancel();
      for (const tab of tabs) {
        const selected = tab === target;
        tab.setAttribute('aria-selected', String(selected)); tab.tabIndex = selected ? 0 : -1;
        document.getElementById(tab.getAttribute('aria-controls')).hidden = !selected;
      }
      current = target;
      if (!reducedMotion()) animation = document.getElementById(target.getAttribute('aria-controls')).animate(
        [{opacity: 0, transform: 'translateY(4px)'}, {opacity: 1, transform: 'translateY(0)'}], {duration: 160, easing: 'ease-out'});
    }
    if (focus) target.focus();
  };
  tabs.forEach((tab, index) => {
    tab.addEventListener('click', () => select(tab.id.slice(4)));
    tab.addEventListener('keydown', event => {
      const offset = event.key === 'ArrowRight' ? 1 : event.key === 'ArrowLeft' ? -1 : 0;
      if (!offset && event.key !== 'Home' && event.key !== 'End') return;
      event.preventDefault();
      const next = event.key === 'Home' ? 0 : event.key === 'End' ? tabs.length - 1 : (index + offset + tabs.length) % tabs.length;
      select(tabs[next].id.slice(4), true);
    });
  });
  return {select};
}
