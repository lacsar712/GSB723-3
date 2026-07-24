const CreditUI = (() => {
  let onCreditRefresh = null;
  let onOrdersRefresh = null;

  function setCallbacks(cbs) {
    onCreditRefresh = cbs.onCreditRefresh || null;
    onOrdersRefresh = cbs.onOrdersRefresh || null;
  }

  function showToast(msg, type = 'info') {
    let toast = document.getElementById('toast');
    if (!toast) {
      toast = document.createElement('div');
      toast.id = 'toast';
      toast.className = 'toast hidden';
      document.body.appendChild(toast);
    }
    toast.textContent = msg;
    toast.style.background = type === 'error' ? '#ef4444' : type === 'success' ? '#10b981' : '#1e293b';
    toast.classList.remove('hidden');
    clearTimeout(toast._t);
    toast._t = setTimeout(() => toast.classList.add('hidden'), 3000);
  }

  function buildStars(container, initialScore = 0, interactive = true) {
    container.innerHTML = '';
    let score = initialScore;
    const stars = [];
    for (let i = 1; i <= 5; i++) {
      const s = document.createElement('i');
      s.className = 'fas fa-star rating-star';
      s.dataset.val = i;
      if (interactive) {
        s.onclick = () => {
          score = i;
          updateStars();
        };
        s.onmouseenter = () => highlightStars(i);
        s.onmouseleave = () => updateStars();
      }
      stars.push(s);
      container.appendChild(s);
    }
    function highlightStars(n) {
      stars.forEach((st, idx) => {
        st.classList.toggle('active', idx < n);
      });
    }
    function updateStars() {
      highlightStars(score);
    }
    updateStars();
    return {
      getScore: () => score,
      setScore: (n) => { score = n; updateStars(); }
    };
  }

  function renderStaticStars(n) {
    let html = '';
    for (let i = 1; i <= 5; i++) {
      html += `<i class="fas fa-star rating-star ${i <= n ? 'active' : ''}" style="${i <= n ? '' : 'color:#e2e8f0;cursor:default'}"></i>`;
    }
    return html;
  }

  async function loadCreditProfile(username) {
    const section = document.getElementById('credit-tab');
    if (!section) return;
    try {
      const credit = await CreditAPI.fetchCredit(username);
      const ratings = await CreditAPI.fetchRatings(username, 'received', 10);
      const given = await CreditAPI.fetchRatings(username, 'given', 10);

      if (credit.status !== 'success') {
        showToast('加载信用档案失败', 'error');
        return;
      }

      const lv = CreditAPI.getLevel(credit.creditScore);
      const statEl = document.getElementById('credit-score-big');
      if (statEl) statEl.textContent = credit.creditScore;
      const levelEl = document.getElementById('credit-level-big');
      if (levelEl) {
        levelEl.textContent = lv.label;
        levelEl.style.color = lv.color;
      }
      const canAcceptEl = document.getElementById('credit-can-accept');
      if (canAcceptEl) {
        canAcceptEl.textContent = credit.canAccept ? '可接单' : '已限制接单';
        canAcceptEl.style.color = credit.canAccept ? '#10b981' : '#ef4444';
      }

      renderRatingList(ratings);

      const toggleReceived = document.getElementById('credit-toggle-received');
      const toggleGiven = document.getElementById('credit-toggle-given');
      if (toggleReceived && toggleGiven) {
        toggleReceived.onclick = () => {
          toggleReceived.classList.add('active');
          toggleGiven.classList.remove('active');
          renderRatingList(ratings);
        };
        toggleGiven.onclick = () => {
          toggleGiven.classList.add('active');
          toggleReceived.classList.remove('active');
          renderRatingList(given);
        };
      }
    } catch (err) {
      console.error(err);
      showToast('加载信用档案失败', 'error');
    }
  }

  function renderRatingList(list) {
    const container = document.getElementById('credit-ratings-list');
    if (!container) return;
    container.innerHTML = '';
    if (!list || list.length === 0) {
      container.innerHTML = '<div class="empty-hint">暂无评分记录</div>';
      return;
    }
    list.forEach(r => {
      const card = document.createElement('div');
      card.className = 'rating-item';
      card.innerHTML = `
        <div class="rating-item-top">
          <span class="rating-from"><i class="fas fa-user"></i> ${r.raterMasked}</span>
          <span class="rating-stars">${renderStaticStars(r.score)}</span>
        </div>
        ${r.comment ? `<div class="rating-comment">&ldquo;${r.comment}&rdquo;</div>` : '<div class="rating-comment" style="color:#94a3b8;font-style:italic;">（无评语）</div>'}
        <div class="rating-meta">
          <span><i class="fas fa-hashtag"></i> 订单 #${r.orderId}</span>
          <span><i class="far fa-clock"></i> ${CreditAPI.formatTime(r.createdAt)}</span>
        </div>
      `;
      container.appendChild(card);
    });
  }

  async function loadDisputeCenter(username) {
    const section = document.getElementById('dispute-tab');
    if (!section) return;
    try {
      await CreditAPI.checkDisputes();
      const disputes = await CreditAPI.fetchDisputes(username);
      const container = document.getElementById('dispute-list');
      if (!container) return;
      container.innerHTML = '';
      if (!disputes || disputes.length === 0) {
        container.innerHTML = '<div class="empty-hint">暂无纠纷记录</div>';
        return;
      }
      disputes.sort((a, b) => b.createdAt - a.createdAt).forEach(d => {
        const statusCls = d.status === 'established' ? 'dispute-established'
          : d.status === 'rejected' ? 'dispute-rejected' : 'dispute-pending';
        const card = document.createElement('div');
        card.className = `dispute-item ${statusCls}`;
        card.innerHTML = `
          <div class="dispute-item-top">
            <span class="dispute-id"><i class="fas fa-gavel"></i> 纠纷 #${d.id} · 订单 #${d.orderId}</span>
            <span class="dispute-status-badge ${statusCls}">${CreditAPI.statusLabel(d.status)}</span>
          </div>
          <div class="dispute-reason">${d.reason}</div>
          <div class="dispute-meta">
            <span><i class="fas fa-user"></i> 发起人: ${d.initiator}</span>
            <span><i class="far fa-clock"></i> ${CreditAPI.formatTime(d.createdAt)}</span>
          </div>
          ${d.status !== 'pending' ? `<div class="dispute-resolution"><i class="fas fa-balance-scale"></i> ${d.resolution || '已裁决'}</div>` : ''}
          <button class="btn-outline dispute-detail-btn" data-id="${d.id}">查看详情</button>
        `;
        container.appendChild(card);
      });
      container.querySelectorAll('.dispute-detail-btn').forEach(btn => {
        btn.onclick = () => openDisputeDetail(parseInt(btn.dataset.id));
      });
    } catch (err) {
      console.error(err);
      showToast('加载纠纷中心失败', 'error');
    }
  }

  async function openDisputeDetail(id) {
    try {
      const d = await CreditAPI.fetchDisputeDetail(id);
      if (d.status === 'error' || !d.id) {
        showToast('纠纷详情加载失败', 'error');
        return;
      }
      const modal = document.getElementById('dispute-detail-modal');
      const body = document.getElementById('dispute-detail-body');
      const statusCls = d.status === 'established' ? 'dispute-established'
        : d.status === 'rejected' ? 'dispute-rejected' : 'dispute-pending';
      body.innerHTML = `
        <div class="dd-header">
          <h3>纠纷 #${d.id}</h3>
          <span class="dispute-status-badge ${statusCls}">${CreditAPI.statusLabel(d.status)}</span>
        </div>
        <div class="dd-section">
          <label>纠纷原因</label>
          <p>${d.reason}</p>
        </div>
        <div class="dd-section">
          <label>关联订单</label>
          ${d.order ? `
            <div class="dd-order">
              <div><strong>#${d.order.id}</strong> ${d.order.package}</div>
              <div class="dd-order-meta">
                <span><i class="fas fa-map-marker-alt"></i> ${d.order.pickup} → ${d.order.delivery}</span>
                <span><i class="fas fa-yen-sign"></i> ${d.order.reward}</span>
              </div>
              <div class="dd-order-meta">
                <span>发布方: ${d.order.creator}（信用${d.order.creatorCredit}）</span>
                <span>接单方: ${d.order.worker || '无'}（信用${d.order.workerCredit}）</span>
              </div>
              <div class="dd-order-meta">
                <span>订单状态: <strong>${CreditAPI.orderStatusLabel(d.order.status)}</strong></span>
              </div>
            </div>
          ` : '<p>订单信息不可用</p>'}
        </div>
        <div class="dd-section">
          <label>发起时间</label>
          <p>${CreditAPI.formatTime(d.createdAt)}</p>
        </div>
        ${d.status !== 'pending' ? `
          <div class="dd-section">
            <label>裁决结果</label>
            <p><strong>${CreditAPI.statusLabel(d.status)}</strong></p>
            <p style="margin-top:6px;color:#64748b;">${d.resolution || ''}</p>
            <p style="color:#94a3b8;font-size:0.85rem;">裁决时间: ${CreditAPI.formatTime(d.resolvedAt)}</p>
          </div>
        ` : `
          <div class="dd-section dd-pending-hint">
            <i class="fas fa-hourglass-half"></i> 纠纷待裁决中，24小时内未补充说明将按接单方责任自动成立
          </div>
        `}
      `;
      modal.classList.remove('hidden');
    } catch (err) {
      console.error(err);
      showToast('加载详情失败', 'error');
    }
  }

  function openRatingModal(orderId, targetUser, targetLabel, currentUser, onSubmitted) {
    const modal = document.getElementById('rating-modal');
    const title = document.getElementById('rating-modal-title');
    const target = document.getElementById('rating-target-user');
    const starsContainer = document.getElementById('rating-stars');
    const commentInput = document.getElementById('rating-comment');
    const submitBtn = document.getElementById('rating-submit-btn');

    title.textContent = `评价${targetLabel}`;
    target.textContent = targetUser;
    commentInput.value = '';
    const stars = buildStars(starsContainer, 0, true);

    modal.classList.remove('hidden');

    submitBtn.onclick = async () => {
      const score = stars.getScore();
      if (score < 1) {
        showToast('请选择星级评分', 'error');
        return;
      }
      const comment = commentInput.value.trim().slice(0, 50);
      try {
        const result = await CreditAPI.submitRating({
          orderId, rater: currentUser.username, ratee: targetUser,
          score, comment
        });
        if (result.status === 'success') {
          showToast('评价提交成功！', 'success');
          modal.classList.add('hidden');
          if (onSubmitted) onSubmitted();
          if (onCreditRefresh) onCreditRefresh();
        } else {
          showToast(result.message || '评价失败', 'error');
        }
      } catch (err) {
        showToast('评价提交失败', 'error');
      }
    };
  }

  function openDisputeModal(orderId, currentUser, onSubmitted) {
    const modal = document.getElementById('dispute-create-modal');
    const orderIdEl = document.getElementById('dispute-order-id');
    const reasonInput = document.getElementById('dispute-reason-input');
    const submitBtn = document.getElementById('dispute-submit-btn');

    orderIdEl.textContent = orderId;
    reasonInput.value = '';
    modal.classList.remove('hidden');

    submitBtn.onclick = async () => {
      const reason = reasonInput.value.trim();
      if (!reason) {
        showToast('请填写纠纷原因', 'error');
        return;
      }
      try {
        const result = await CreditAPI.createDispute({
          orderId, initiator: currentUser.username, reason: reason.slice(0, 100)
        });
        if (result.status === 'success') {
          showToast('纠纷已提交，等待裁决', 'success');
          modal.classList.add('hidden');
          if (onSubmitted) onSubmitted();
          if (onOrdersRefresh) onOrdersRefresh();
        } else {
          showToast(result.message || '提交失败', 'error');
        }
      } catch (err) {
        showToast('提交纠纷失败', 'error');
      }
    };
  }

  function closeAllModals() {
    ['rating-modal', 'dispute-create-modal', 'dispute-detail-modal'].forEach(id => {
      const m = document.getElementById(id);
      if (m) m.classList.add('hidden');
    });
  }

  function buildCreditBadge(level) {
    return `<span class="credit-badge credit-level-${level.level}">${level.label}</span>`;
  }

  function initModalClose() {
    document.querySelectorAll('[data-close-modal]').forEach(btn => {
      btn.onclick = () => closeAllModals();
    });
  }

  return {
    setCallbacks,
    showToast,
    buildStars,
    renderStaticStars,
    loadCreditProfile,
    loadDisputeCenter,
    openRatingModal,
    openDisputeModal,
    openDisputeDetail,
    closeAllModals,
    buildCreditBadge,
    initModalClose
  };
})();
