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
    function openDisputeDetail(id) {
        const d = lastDisputes.find(x => x.id === id);
        if (!d) return;
        const body = document.getElementById('dispute-detail-body');

        let verdictBlock = '';
        if (d.status === 'pending') {
            verdictBlock = `
                <div class="dispute-detail-row">
                    <span class="k">裁决结果</span>
                    <span class="v">待裁决 —— 24 小时内双方未补充说明将按接单方责任自动成立</span>
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
            ${verdictBlock}
        `;

        document.getElementById('dispute-detail-modal').classList.remove('hidden');

        if (d.status === 'pending') {
            document.getElementById('detail-uphold').onclick = () => arbitrate(d.id, 'upheld');
            document.getElementById('detail-reject').onclick = () => arbitrate(d.id, 'rejected');
        }
    }

    async function arbitrate(id, verdict) {
        const { ok, data } = await API.arbitrateDispute(id, verdict);
        if (ok && data.status === 'success') {
            document.getElementById('dispute-detail-modal').classList.add('hidden');
            ctx.toast(verdict === 'upheld' ? '纠纷已成立，订单已退市' : '纠纷已驳回，订单恢复流转');
            loadDisputes();
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
        loadDisputes,
        openRatingModal,
        openDisputeModal,
        creditTagHtml,
        lowCreditNoticeHtml,
        starsHtml
    };
})();
