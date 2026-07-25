/*
 * credit-ui.js
 * 「信用分 + 纠纷仲裁」模块 —— 界面渲染 与 交互绑定 层。
 * 负责信用档案页、纠纷中心、评价弹窗、纠纷弹窗、大厅信用标识等 DOM 操作。
 * 通过 credit-api.js 拿数据与规则，通过 init() 从 script.js 注入共享上下文。
 */
(function () {
    'use strict';

    const API = window.CreditAPI;

    // 由 script.js 注入的共享上下文
    let ctx = {
        getUser: () => null,   // 返回当前登录用户对象
        toast: (m) => alert(m),// Toast 反馈
        onCreditChanged: () => { } // 信用分变化后回调（刷新本地缓存/侧边栏）
    };

    let creditDirection = 'received'; // received | given
    let ratingContext = null;         // 当前评价弹窗上下文 { orderId, targetName, refresh }
    let ratingScore = 0;              // 当前选择的星级
    let disputeContext = null;        // 当前发起纠纷上下文 { orderId, targetName, refresh }
    let lastDisputes = [];            // 最近一次拉取的纠纷列表，用于详情弹窗查找

    /* ---------- 星级只读组件 ---------- */
    function starsHtml(score) {
        let h = '<span class="stars">';
        for (let i = 1; i <= 5; i++) {
            h += `<i class="fas fa-star${i <= score ? '' : ' dim'}"></i>`;
        }
        return h + '</span>';
    }

    /* ---------- 信用档案页 ---------- */
    async function loadCreditProfile() {
        const user = ctx.getUser();
        if (!user) return;
        const badge = document.getElementById('credit-level-badge');
        const val = document.getElementById('credit-score-val');
        const levelText = document.getElementById('credit-level-text');
        const recordsEl = document.getElementById('credit-records');
        const card = document.getElementById('credit-score-card');

        recordsEl.innerHTML = '<div style="color:#64748b;padding:20px;">加载中...</div>';

        try {
            const data = await API.fetchCredit(user.username, creditDirection);
            const info = API.levelInfo(data.credit);

            badge.textContent = info.label + ' 级';
            val.textContent = data.credit;
            let text = info.text;
            if (!data.canAccept) {
                text += '（信用分低于 60，暂不可接单）';
            }
            levelText.textContent = text;
            card.style.background = data.credit < 60
                ? 'linear-gradient(135deg,#ef4444,#f97316)'
                : 'linear-gradient(135deg,var(--primary),#8b5cf6)';

            // 同步更新本地登录态里的信用分
            if (typeof data.credit === 'number') {
                user.credit = data.credit;
                ctx.onCreditChanged(data.credit);
            }

            renderCreditRecords(data.records || []);
        } catch (e) {
            recordsEl.innerHTML = '<div style="color:#ef4444;padding:20px;">信用数据加载失败</div>';
        }

        // 事件时间线始终展示该用户全部事件（与评分方向切换无关）
        loadTimeline();
    }

    function renderCreditRecords(records) {
        const el = document.getElementById('credit-records');
        if (!records.length) {
            el.innerHTML = '<div style="color:#64748b;padding:20px;text-align:center;">暂无评分记录</div>';
            return;
        }
        const label = creditDirection === 'received' ? '评分人' : '被评价人';
        el.innerHTML = records.map(r => `
            <div class="credit-record">
                <div class="credit-record-top">
                    <span class="credit-record-name">${label}：${escapeHtml(r.counterpart)}</span>
                    ${starsHtml(r.score)}
                </div>
                ${r.comment ? `<div class="credit-record-comment">“${escapeHtml(r.comment)}”</div>` : ''}
                <div class="credit-record-meta">
                    <span><i class="fas fa-hashtag"></i> 订单 #${r.orderId}</span>
                    <span><i class="far fa-clock"></i> ${API.formatTime(r.time)}</span>
                </div>
            </div>
        `).join('');
    }

    /* ---------- 信用事件时间线 ---------- */
    async function loadTimeline() {
        const user = ctx.getUser();
        if (!user) return;
        const el = document.getElementById('credit-timeline');
        el.innerHTML = '<div style="color:#64748b;padding:20px;">加载中...</div>';
        try {
            const events = await API.fetchCreditEvents(user.username, 50);
            renderTimeline(events || []);
        } catch (e) {
            el.innerHTML = '<div style="color:#ef4444;padding:20px;">时间线加载失败</div>';
        }
    }

    function renderTimeline(events) {
        const el = document.getElementById('credit-timeline');
        if (!events.length) {
            el.innerHTML = '<div style="color:#64748b;padding:20px;text-align:center;">暂无信用事件</div>';
            return;
        }
        el.innerHTML = events.map(ev => {
            const m = API.metaOf(ev.type);
            // 信用分变更：突出显示变更前后分值
            let extra = '';
            if (ev.type === 'credit_change' && ev.oldCredit >= 0 && ev.newCredit >= 0) {
                const up = ev.newCredit >= ev.oldCredit;
                const oldLv = API.levelInfo(ev.oldCredit).label;
                const newLv = API.levelInfo(ev.newCredit).label;
                extra = `<div class="tl-credit-change">
                    <span class="tl-old">${ev.oldCredit} <small>${oldLv}</small></span>
                    <i class="fas fa-arrow-right"></i>
                    <span class="tl-new ${up ? 'up' : 'down'}">${ev.newCredit} <small>${newLv}</small></span>
                </div>`;
            }
            const refLabel = ev.refType === 'dispute'
                ? `纠纷 #${ev.refId}` : `订单 #${ev.refId}`;
            return `
            <div class="timeline-item" data-ref-type="${ev.refType}" data-ref-id="${ev.refId}">
                <span class="timeline-dot" style="background:${m.color}">
                    <i class="fas ${m.icon}"></i>
                </span>
                <div class="timeline-body">
                    <div class="timeline-head">
                        <span class="timeline-title">${m.title}</span>
                        <span class="timeline-time">${API.formatTime(ev.time)}</span>
                    </div>
                    <div class="timeline-detail">${escapeHtml(ev.detail)}</div>
                    ${extra}
                    <span class="timeline-link"><i class="fas fa-arrow-up-right-from-square"></i> 查看${refLabel}</span>
                </div>
            </div>`;
        }).join('');

        el.querySelectorAll('.timeline-item').forEach(item => {
            item.onclick = () => {
                const refType = item.dataset.refType;
                const refId = parseInt(item.dataset.refId, 10);
                if (refType === 'dispute') {
                    ctx.openDisputeById(refId);
                } else {
                    ctx.gotoOrder(refId);
                }
            };
        });
    }

    function bindCreditToggle() {
        const recv = document.getElementById('credit-received');
        const given = document.getElementById('credit-given');
        recv.onclick = () => {
            recv.classList.add('active');
            given.classList.remove('active');
            creditDirection = 'received';
            loadCreditProfile();
        };
        given.onclick = () => {
            given.classList.add('active');
            recv.classList.remove('active');
            creditDirection = 'given';
            loadCreditProfile();
        };
    }

    /* ---------- 评价弹窗 ---------- */
    function openRatingModal(orderId, targetName, refresh) {
        ratingContext = { orderId, targetName, refresh };
        ratingScore = 0;
        document.getElementById('rating-target-info').textContent =
            `订单 #${orderId} · 评价「${targetName}」`;
        document.getElementById('rating-comment').value = '';
        paintStarPicker(0);
        document.getElementById('rating-modal').classList.remove('hidden');
    }

    function paintStarPicker(score) {
        document.querySelectorAll('#rating-stars i').forEach(i => {
            const v = parseInt(i.dataset.val, 10);
            i.className = v <= score ? 'fas fa-star' : 'far fa-star';
        });
    }

    function bindRatingModal() {
        document.querySelectorAll('#rating-stars i').forEach(star => {
            star.onclick = () => {
                ratingScore = parseInt(star.dataset.val, 10);
                paintStarPicker(ratingScore);
            };
            star.onmouseover = () => paintStarPicker(parseInt(star.dataset.val, 10));
            star.onmouseout = () => paintStarPicker(ratingScore);
        });

        document.getElementById('rating-form').onsubmit = async (e) => {
            e.preventDefault();
            if (!ratingContext) return;
            if (ratingScore < 1 || ratingScore > 5) {
                ctx.toast('请先选择 1-5 星评分');
                return;
            }
            const payload = {
                orderId: ratingContext.orderId,
                rater: ctx.getUser().username,
                score: ratingScore,
                comment: document.getElementById('rating-comment').value.trim()
            };
            const { ok, data } = await API.submitRating(payload);
            if (ok && data.status === 'success') {
                document.getElementById('rating-modal').classList.add('hidden');
                ctx.toast('评价成功，已更新对方信用分！');
                if (ratingContext.refresh) ratingContext.refresh();
            } else {
                ctx.toast((data && data.message) || '评价失败');
            }
        };
    }

    /* ---------- 发起纠纷弹窗 ---------- */
    function openDisputeModal(orderId, targetName, refresh) {
        disputeContext = { orderId, targetName, refresh };
        document.getElementById('dispute-target-info').textContent =
            `订单 #${orderId}${targetName ? ' · 对方：' + targetName : ''}`;
        document.getElementById('dispute-reason').value = '';
        document.getElementById('dispute-modal').classList.remove('hidden');
    }

    function bindDisputeModal() {
        document.getElementById('dispute-form').onsubmit = async (e) => {
            e.preventDefault();
            if (!disputeContext) return;
            const reason = document.getElementById('dispute-reason').value.trim();
            if (!reason) {
                ctx.toast('请填写纠纷原因');
                return;
            }
            const payload = {
                orderId: disputeContext.orderId,
                initiator: ctx.getUser().username,
                reason: reason
            };
            const { ok, data } = await API.createDispute(payload);
            if (ok && data.status === 'success') {
                document.getElementById('dispute-modal').classList.add('hidden');
                ctx.toast('纠纷已发起，订单已冻结，等待裁决');
                if (disputeContext.refresh) disputeContext.refresh();
            } else {
                ctx.toast((data && data.message) || '发起纠纷失败');
            }
        };
    }

    /* ---------- 纠纷中心列表 ---------- */
    async function loadDisputes() {
        const user = ctx.getUser();
        if (!user) return;
        const el = document.getElementById('dispute-list');
        el.innerHTML = '<div style="color:#64748b;padding:20px;">加载中...</div>';
        try {
            const list = await API.fetchDisputes(user.username);
            lastDisputes = list;
            if (!list.length) {
                el.innerHTML = '<div style="color:#64748b;padding:30px;text-align:center;">暂无纠纷记录</div>';
                return;
            }
            el.innerHTML = list.map(d => `
                <div class="dispute-card ${d.status}" data-id="${d.id}">
                    <div class="dispute-card-top">
                        <strong>订单 #${d.orderId} · ${escapeHtml(d.package || '')}</strong>
                        <span class="dispute-status ${d.status}">${API.disputeStatusMap[d.status] || d.status}</span>
                    </div>
                    <div class="dispute-reason">发起人 ${escapeHtml(d.initiator)}：${escapeHtml(d.reason)}</div>
                    <div class="dispute-meta"><i class="far fa-clock"></i> ${API.formatTime(d.createdAt)}</div>
                </div>
            `).join('');

            el.querySelectorAll('.dispute-card').forEach(card => {
                card.onclick = () => openDisputeDetail(parseInt(card.dataset.id, 10));
            });
        } catch (e) {
            el.innerHTML = '<div style="color:#ef4444;padding:20px;">纠纷数据加载失败</div>';
        }
    }

    /* ---------- 纠纷详情弹窗（含手动裁决触发） ---------- */
    // 由时间线跳转调用：确保先拿到纠纷数据，再打开详情弹窗
    async function openDisputeById(id) {
        const user = ctx.getUser();
        if (!lastDisputes.length || !lastDisputes.find(x => x.id === id)) {
            try { lastDisputes = await API.fetchDisputes(user.username); } catch (e) { /* ignore */ }
        }
        openDisputeDetail(id);
    }

    function openDisputeDetail(id) {
        const d = lastDisputes.find(x => x.id === id);
        if (!d) return;
        const body = document.getElementById('dispute-detail-body');
        const user = ctx.getUser();

        let verdictBlock = '';
        if (d.status === 'pending') {
            verdictBlock = `
                <div class="dispute-detail-row">
                    <span class="k">裁决结果</span>
                    <span class="v">待裁决 —— 双方均未补充说明且超过 24 小时，将按接单方责任自动成立</span>
                </div>
                <div style="display:flex;gap:10px;margin-top:8px;">
                    <button class="btn-outline" style="color:#dc2626;border-color:#fecaca"
                        id="detail-uphold">判定成立（退款）</button>
                    <button class="btn-outline" style="color:#059669;border-color:#bbf7d0"
                        id="detail-reject">驳回纠纷</button>
                </div>`;
        } else {
            verdictBlock = `
                <div class="dispute-detail-row">
                    <span class="k">裁决结果</span>
                    <span class="v">${API.disputeStatusMap[d.status]}</span>
                </div>
                <div class="dispute-verdict-box">${escapeHtml(d.verdict || '')}</div>
                <div class="dispute-detail-row" style="margin-top:12px;">
                    <span class="k">裁决时间</span>
                    <span class="v">${API.formatTime(d.resolvedAt)}</span>
                </div>`;
        }

        body.innerHTML = `
            <div class="dispute-detail-row">
                <span class="k">关联订单</span>
                <span class="v">#${d.orderId} · ${escapeHtml(d.package || '')} · 悬赏 ${escapeHtml(d.reward || '')}</span>
            </div>
            <div class="dispute-detail-row">
                <span class="k">发布方 / 接单方</span>
                <span class="v">${escapeHtml(d.creator || '-')} / ${escapeHtml(d.worker || '-')}</span>
            </div>
            <div class="dispute-detail-row">
                <span class="k">发起人</span>
                <span class="v">${escapeHtml(d.initiator)}</span>
            </div>
            <div class="dispute-detail-row">
                <span class="k">纠纷原因</span>
                <span class="v">${escapeHtml(d.reason)}</span>
            </div>
            <div class="dispute-detail-row">
                <span class="k">发起时间</span>
                <span class="v">${API.formatTime(d.createdAt)}</span>
            </div>
            ${renderStatements(d)}
            ${renderStatementForm(d, user)}
            ${verdictBlock}
        `;

        document.getElementById('dispute-detail-modal').classList.remove('hidden');

        if (d.status === 'pending') {
            document.getElementById('detail-uphold').onclick = () => arbitrate(d.id, 'upheld');
            document.getElementById('detail-reject').onclick = () => arbitrate(d.id, 'rejected');
            bindStatementForm(d);
        }
    }

    // 渲染双方补充说明
    function renderStatements(d) {
        const list = d.statements || [];
        let inner;
        if (!list.length) {
            inner = '<div class="stmt-empty">暂无补充说明</div>';
        } else {
            inner = list.map(s => {
                const evi = s.evidenceType
                    ? `<div class="stmt-evidence"><i class="fas fa-paperclip"></i>
                        ${API.evidenceTypeLabel[s.evidenceType] || s.evidenceType}${s.evidenceDesc ? '：' + escapeHtml(s.evidenceDesc) : ''}</div>`
                    : '';
                return `
                <div class="stmt-item ${s.role}">
                    <div class="stmt-head">
                        <span class="stmt-role ${s.role}">${API.roleLabel[s.role] || s.role} · ${escapeHtml(s.author)}</span>
                        <span class="stmt-time">${API.formatTime(s.createdAt)}</span>
                    </div>
                    <div class="stmt-content">${escapeHtml(s.content)}</div>
                    ${evi}
                </div>`;
            }).join('');
        }
        return `<div class="dispute-detail-row">
                    <span class="k">补充说明</span>
                    <div class="stmt-list">${inner}</div>
                </div>`;
    }

    // 渲染补充说明提交表单：仅待裁决、当前用户为参与方且该角色尚未提交时显示
    function renderStatementForm(d, user) {
        if (d.status !== 'pending' || !user) return '';
        let myRole = '';
        if (d.creator === user.username) myRole = 'creator';
        else if (d.worker === user.username) myRole = 'worker';
        if (!myRole) return ''; // 非参与方（如管理员查看）不显示表单
        const submitted = (d.statements || []).some(s => s.role === myRole);
        if (submitted) {
            return `<div class="stmt-done"><i class="fas fa-check-circle"></i> 你（${API.roleLabel[myRole]}）已提交补充说明</div>`;
        }
        return `
            <div class="stmt-form">
                <div class="stmt-form-title"><i class="fas fa-pen-to-square"></i> 提交补充说明（${API.roleLabel[myRole]}，仅一次）</div>
                <textarea id="stmt-content" maxlength="200" rows="3"
                    placeholder="补充陈述你的情况（≤200字）..."></textarea>
                <div class="stmt-evidence-row">
                    <select id="stmt-evidence-type">
                        <option value="">证据类型（可选）</option>
                        <option value="screenshot">截图说明</option>
                        <option value="chatlog">聊天记录</option>
                        <option value="other">其他</option>
                    </select>
                    <input type="text" id="stmt-evidence-desc" maxlength="50"
                        placeholder="证据一句话说明（≤50字，可选）">
                </div>
                <button class="btn-primary" id="stmt-submit" style="margin-top:10px;">提交补充说明</button>
            </div>`;
    }

    function bindStatementForm(d) {
        const btn = document.getElementById('stmt-submit');
        if (!btn) return;
        btn.onclick = async () => {
            const content = document.getElementById('stmt-content').value.trim();
            if (!content) { ctx.toast('请填写补充说明内容'); return; }
            const payload = {
                disputeId: d.id,
                author: ctx.getUser().username,
                content: content,
                evidenceType: document.getElementById('stmt-evidence-type').value,
                evidenceDesc: document.getElementById('stmt-evidence-desc').value.trim()
            };
            const { ok, data } = await API.submitStatement(payload);
            if (ok && data.status === 'success') {
                ctx.toast('补充说明已提交');
                // 重新拉取纠纷数据并刷新详情弹窗
                try { lastDisputes = await API.fetchDisputes(ctx.getUser().username); } catch (e) { /* ignore */ }
                openDisputeDetail(d.id);
                if (!document.getElementById('disputes-tab').classList.contains('hidden')) {
                    loadDisputes();
                }
            } else {
                ctx.toast((data && data.message) || '提交失败');
            }
        };
    }

    async function arbitrate(id, verdict) {
        const { ok, data } = await API.arbitrateDispute(id, verdict);
        if (ok && data.status === 'success') {
            document.getElementById('dispute-detail-modal').classList.add('hidden');
            ctx.toast(verdict === 'upheld' ? '纠纷已成立，订单已退市' : '纠纷已驳回，订单恢复流转');
            loadDisputes();
            // 若信用档案页可见，同步刷新时间线
            if (!document.getElementById('credit-tab').classList.contains('hidden')) {
                loadTimeline();
            }
        } else {
            ctx.toast((data && data.message) || '裁决失败');
        }
    }

    /* ---------- 大厅信用标识（供 script.js 渲染卡片时调用） ---------- */
    // 返回信用短标签 HTML
    function creditTagHtml(levelKey, levelLabel) {
        return `<span class="credit-tag ${levelKey}">信用 ${levelLabel}</span>`;
    }

    // 低信用发布弱提示 HTML（列表顶部）
    function lowCreditNoticeHtml() {
        return `<div class="low-credit-notice">
            <i class="fas fa-exclamation-triangle"></i>
            列表中含「低信用发布」任务，接单前请注意核对信息。
        </div>`;
    }

    /* ---------- 任务大厅：分组渲染 + 筛选 + 发布者小面板 ---------- */
    const statusMap = { pending: '待接单', accepted: '进行中', delivered: '待收货', completed: '已完成', cancelled: '已撤回' };

    // 渲染大厅（含「全部 / 仅优良 / 低信用专区」分组）。
    // orders: 已过滤为 pending 且非本人的大厅订单；filter: 'all'|'good'|'low'
    function renderDashboard(orders, container, filter) {
        container.innerHTML = '';
        const { normal, low } = API.partitionDashboardOrders(orders, filter);

        if (normal.length === 0 && low.length === 0) {
            container.innerHTML = '<div class="dashboard-empty">暂无符合条件的任务</div>';
            return;
        }

        // 正常任务
        normal.forEach(o => container.appendChild(buildDashboardCard(o)));

        // 低信用专区（默认折叠）
        if (low.length > 0) {
            const wrap = document.createElement('div');
            wrap.className = 'low-zone';
            wrap.innerHTML = `
                <div class="low-zone-header" id="low-zone-toggle">
                    <span><i class="fas fa-triangle-exclamation"></i> 低信用专区（${low.length}）</span>
                    <i class="fas fa-chevron-down low-zone-caret"></i>
                </div>
                <div class="low-zone-hint">以下任务的发布者信用分低于 60，默认折叠，接单前请谨慎核对。</div>
                <div class="order-grid low-zone-grid hidden" id="low-zone-grid"></div>`;
            container.appendChild(wrap);
            const grid = wrap.querySelector('#low-zone-grid');
            low.forEach(o => grid.appendChild(buildDashboardCard(o)));
            const header = wrap.querySelector('#low-zone-toggle');
            const caret = wrap.querySelector('.low-zone-caret');
            header.onclick = () => {
                grid.classList.toggle('hidden');
                caret.classList.toggle('open');
            };
        }
    }

    // 构建一张大厅订单卡片（含等级短标签 + 信用小面板触发）
    function buildDashboardCard(order) {
        const card = document.createElement('div');
        const isLow = API.isLowCreditPublisher(order.creatorCredit);
        card.className = `order-card ${order.status}${isLow ? ' low-credit' : ''}`;
        card.dataset.orderId = order.id;

        const creditTag = creditTagHtml(order.creatorLevelKey, order.creatorLevel);
        const lowText = isLow
            ? '<div class="info-row" style="color:#b91c1c;"><i class="fas fa-exclamation-circle"></i> <span>低信用发布</span></div>'
            : '';

        card.innerHTML = `
            <div style="display:flex;justify-content:space-between;align-items:center;">
                <span class="badge ${order.status}">${statusMap[order.status]}</span>
                <span style="color:#f43f5e;font-weight:800;font-size:1.2rem;">${escapeHtml(order.reward)}</span>
            </div>
            <div class="order-body">
                <h3>${escapeHtml(order.package)}</h3>
                <div class="info-row"><i class="fas fa-map-marker-alt"></i> <span>${escapeHtml(order.pickup)}</span></div>
                <div class="info-row"><i class="fas fa-door-open"></i> <span>送至: ${escapeHtml(order.delivery)}</span></div>
                <div class="info-row">
                    <i class="fas fa-user-circle"></i>
                    <span>发布人: ${escapeHtml(order.creator)}${creditTag}
                        <button class="pub-info-btn" title="查看发布者信用">
                            <i class="fas fa-circle-info"></i>
                        </button>
                    </span>
                </div>
                ${lowText}
            </div>
            <div class="pub-panel hidden"></div>
            <div class="order-footer">
                <button class="btn-primary btn-accept">确认接单</button>
            </div>`;

        // 接单：委托给 script.js 注入的回调
        card.querySelector('.btn-accept').onclick = () => ctx.acceptOrder(order.id);

        // 信用小面板：点击切换，按需拉取发布者信用摘要
        const panel = card.querySelector('.pub-panel');
        const btn = card.querySelector('.pub-info-btn');
        btn.onclick = (e) => {
            e.stopPropagation();
            togglePublisherPanel(panel, order.creator);
        };
        return card;
    }

    async function togglePublisherPanel(panel, creator) {
        if (!panel.classList.contains('hidden')) {
            panel.classList.add('hidden');
            return;
        }
        panel.classList.remove('hidden');
        panel.innerHTML = '<div class="pub-loading">加载中...</div>';
        try {
            const s = await API.fetchPublisherSummary(creator);
            panel.innerHTML = renderPublisherPanel(s);
        } catch (e) {
            panel.innerHTML = '<div class="pub-loading" style="color:#ef4444;">信用信息加载失败</div>';
        }
    }

    function renderPublisherPanel(s) {
        const ratings = s.recentRatings.length
            ? s.recentRatings.map(r => `
                <div class="pub-rating">
                    <div class="pub-rating-top">
                        <span>${escapeHtml(r.counterpart)}</span>
                        ${starsHtml(r.score)}
                    </div>
                    ${r.comment ? `<div class="pub-rating-cmt">“${escapeHtml(r.comment)}”</div>` : ''}
                </div>`).join('')
            : '<div class="pub-rating-empty">暂无收到的评价</div>';

        const disputeTag = s.hasActiveDispute
            ? '<span class="pub-dispute yes"><i class="fas fa-gavel"></i> 有进行中纠纷</span>'
            : '<span class="pub-dispute no"><i class="fas fa-circle-check"></i> 无进行中纠纷</span>';

        return `
            <div class="pub-panel-head">
                <span class="credit-tag ${s.levelKey}">信用 ${s.level}</span>
                <span class="pub-score">${s.credit} 分</span>
                ${!s.canAccept ? '<span class="pub-noaccept">已限接单</span>' : ''}
            </div>
            <div class="pub-panel-disp">${disputeTag}</div>
            <div class="pub-panel-title">近 3 条收到的评价</div>
            <div class="pub-ratings">${ratings}</div>`;
    }

    // 发布悬赏页的信用弱警告：< 75 显示，不拦截发布
    function renderPublishWarning(container, credit) {
        const existing = container.querySelector('.publish-warning');
        if (existing) existing.remove();
        if (!API.needsPublishWarning(credit)) return;
        const div = document.createElement('div');
        div.className = 'publish-warning';
        let msg = `你的当前信用分为 ${credit}（${API.levelInfo(credit).label}）。信用越高越容易被接单，请如实描述任务信息。`;
        if (API.isLowCreditPublisher(credit)) {
            msg += ' 由于信用分低于 60，你新发布的待接单任务将进入大厅「低信用专区」并标注「低信用发布」。';
        }
        div.innerHTML = `<i class="fas fa-triangle-exclamation"></i> <span>${msg}</span>`;
        container.insertBefore(div, container.firstChild);
    }

    /* ---------- 工具 ---------- */
    function escapeHtml(str) {
        if (str == null) return '';
        return String(str)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;');
    }

    /* ---------- 初始化 ---------- */
    function init(sharedCtx) {
        ctx = Object.assign(ctx, sharedCtx);
        bindCreditToggle();
        bindRatingModal();
        bindDisputeModal();
    }

    window.CreditUI = {
        init,
        loadCreditProfile,
        loadTimeline,
        loadDisputes,
        openRatingModal,
        openDisputeModal,
        openDisputeById,
        creditTagHtml,
        lowCreditNoticeHtml,
        renderDashboard,
        renderPublishWarning,
        starsHtml
    };
})();
