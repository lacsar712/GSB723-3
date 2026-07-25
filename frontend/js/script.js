document.addEventListener('DOMContentLoaded', () => {
    // State management
    let currentUser = JSON.parse(localStorage.getItem('user')) || null;
    let currentFilter = '';
    let creditFilter = 'all'; // 'all' | 'good' | 'low'  大厅信用筛选
    let myOrdersView = 'created'; // 'created' or 'accepted'

    const elements = {
        authOverlay: document.getElementById('auth-overlay'),
        mainApp: document.getElementById('main-app'),
        loginForm: document.getElementById('login-form'),
        registerForm: document.getElementById('register-form'),
        tabLogin: document.getElementById('tab-login'),
        tabRegister: document.getElementById('tab-register'),
        displayName: document.getElementById('display-name'),
        displayMajor: document.getElementById('display-major'),
        welcomeName: document.getElementById('welcome-name'),
        logoutBtn: document.getElementById('logout-btn'),
        orderList: document.getElementById('order-list'),
        myOrdersList: document.getElementById('my-orders-list'),
        activeCount: document.getElementById('active-count'),
        orderForm: document.getElementById('order-form'),
        navItems: document.querySelectorAll('.nav-item'),
        filterPills: document.querySelectorAll('#filter-pills .pill'),
        creditFilterPills: document.querySelectorAll('#credit-filter-pills .credit-pill'),
        viewSections: document.querySelectorAll('.view-section'),
        showCreated: document.getElementById('show-created'),
        showAccepted: document.getElementById('show-accepted'),
        profileRealName: document.getElementById('profile-realname'),
        profileMajor: document.getElementById('profile-major-disp'),
        toast: document.getElementById('toast'),
        editProfileBtn: document.getElementById('edit-profile-btn'),
        accountSecBtn: document.getElementById('account-sec-btn'),
        profileModal: document.getElementById('profile-modal'),
        securityModal: document.getElementById('security-modal'),
        profileForm: document.getElementById('profile-form'),
        securityForm: document.getElementById('security-form'),
        editRealname: document.getElementById('edit-realname'),
        editMajor: document.getElementById('edit-major'),
        editPassword: document.getElementById('edit-password')
    };

    // --- Authentication ---
    function updateUIForLogin() {
        if (currentUser) {
            elements.authOverlay.classList.add('hidden');
            elements.mainApp.classList.remove('hidden');
            elements.displayName.textContent = currentUser.realName;
            elements.displayMajor.textContent = currentUser.major;
            elements.welcomeName.textContent = currentUser.realName;
            const pkgCustomerDisp = document.getElementById('pkg-customer-disp');
            if (pkgCustomerDisp) pkgCustomerDisp.textContent = currentUser.realName;

            // Go to dashboard by default to clear any previous account's tab state
            document.querySelector('[data-tab="dashboard"]').click();
        } else {
            elements.authOverlay.classList.remove('hidden');
            elements.mainApp.classList.add('hidden');
        }
    }

    elements.tabLogin.onclick = () => {
        elements.loginForm.classList.remove('hidden');
        elements.registerForm.classList.add('hidden');
        elements.tabLogin.classList.add('active');
        elements.tabRegister.classList.remove('active');
    };

    elements.tabRegister.onclick = () => {
        elements.loginForm.classList.add('hidden');
        elements.registerForm.classList.remove('hidden');
        elements.tabLogin.classList.remove('active');
        elements.tabRegister.classList.add('active');
    };

    elements.loginForm.onsubmit = async (e) => {
        e.preventDefault();
        const username = document.getElementById('login-user').value;
        const password = document.getElementById('login-pass').value;
        try {
            const resp = await fetch('/api/login', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ username, password })
            });
            if (!resp.ok) {
                showToast('账号或密码错误');
                return;
            }
            const data = await resp.json();
            if (data.status === 'success') {
                currentUser = data;
                localStorage.setItem('user', JSON.stringify(data));
                updateUIForLogin();
                showToast('登录成功！');
                // Clear form
                elements.loginForm.reset();
            } else {
                showToast('账号或密码错误');
            }
        } catch (err) { showToast('服务器连接失败'); }
    };

    elements.registerForm.onsubmit = async (e) => {
        e.preventDefault();
        const payload = {
            username: document.getElementById('reg-user').value,
            password: document.getElementById('reg-pass').value,
            realName: document.getElementById('reg-name').value,
            major: document.getElementById('reg-major').value
        };
        try {
            const resp = await fetch('/api/register', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            const data = await resp.json();
            if (resp.ok && data.status === 'success') {
                currentUser = data;
                localStorage.setItem('user', JSON.stringify(data));
                updateUIForLogin();
                showToast('注册成功！');
                elements.registerForm.reset();
            } else {
                showToast(data.message || '注册失败');
            }
        } catch (err) { showToast('注册失败'); }
    };

    elements.logoutBtn.onclick = () => {
        localStorage.removeItem('user');
        currentUser = null;
        // Clear sensitive UI elements
        elements.orderList.innerHTML = '';
        elements.myOrdersList.innerHTML = '';
        elements.loginForm.reset();
        elements.registerForm.reset();

        // Ensure returning to login tab
        elements.tabLogin.click();

        updateUIForLogin();
    };

    // --- Navigation & Tabs ---
    elements.navItems.forEach(item => {
        item.onclick = (e) => {
            e.preventDefault();
            const tab = item.dataset.tab;
            elements.navItems.forEach(i => i.classList.remove('active'));
            item.classList.add('active');
            elements.viewSections.forEach(v => v.classList.add('hidden'));
            document.getElementById(`${tab}-tab`).classList.remove('hidden');

            if (tab === 'dashboard') fetchOrders();
            if (tab === 'my-orders') fetchMyOrders();
            if (tab === 'profile') loadProfile();
            if (tab === 'credit') CreditUI.loadCreditProfile();
            if (tab === 'disputes') CreditUI.loadDisputes();
            if (tab === 'post-task') refreshPublishWarning();
        };
    });

    // 发布页信用弱警告：拉取当前用户信用分，交给 CreditUI 渲染
    async function refreshPublishWarning() {
        const container = document.querySelector('#post-task-tab .form-container');
        if (!container) return;
        try {
            const c = await CreditAPI.fetchCredit(currentUser.username, 'received');
            if (typeof c.credit === 'number') {
                currentUser.credit = c.credit;
                localStorage.setItem('user', JSON.stringify(currentUser));
            }
            CreditUI.renderPublishWarning(container, c.credit);
        } catch (e) { /* 静默：网络异常时不阻断发布 */ }
    }

    // --- Order Logic ---
    async function fetchOrders() {
        try {
            let url = `/api/orders?category=${encodeURIComponent(currentFilter)}`;
            const resp = await fetch(url);
            const orders = await resp.json();
            // 信用信息可能已变化，清理发布者面板缓存
            CreditAPI.clearPublisherCache();
            // 大厅只展示待接单且非本人发布的任务；分组/筛选渲染下沉到 CreditUI
            const dashOrders = orders.filter(o => o.status === 'pending' && o.creator !== currentUser.username);
            CreditUI.renderDashboard(dashOrders, elements.orderList, creditFilter);
            elements.activeCount.textContent = orders.filter(o => o.status !== 'completed').length;
        } catch (err) { console.error(err); }
    }

    async function fetchMyOrders() {
        try {
            let url = myOrdersView === 'created'
                ? `/api/orders?creator=${currentUser.username}`
                : `/api/orders?worker=${currentUser.username}`;
            const resp = await fetch(url);
            const orders = await resp.json();
            renderOrders(orders, elements.myOrdersList, true);
        } catch (err) { console.error(err); }
    }

    function renderOrders(orders, container, isMyOrders = false) {
        container.innerHTML = '';
        // 注：大厅（isMyOrders=false）已由 CreditUI.renderDashboard 接管，
        // 这里仅服务「我的订单」列表。

        if (orders.length === 0) {
            container.innerHTML = '<div style="grid-column: 1/-1; text-align: center; color: #64748b; padding: 40px;">暂无订单数据</div>';
            return;
        }

        orders.forEach(order => {
            const card = document.createElement('div');
            card.className = `order-card ${order.status}`;
            card.dataset.orderId = order.id;

            const statusMap = { 'pending': '待接单', 'accepted': '进行中', 'delivered': '待收货', 'completed': '已完成', 'cancelled': '已撤回' };

            card.innerHTML = `
                <div style="display: flex; justify-content: space-between; align-items: center;">
                    <span class="badge ${order.status}">${statusMap[order.status]}</span>
                    <span style="color: #f43f5e; font-weight: 800; font-size: 1.2rem;">${order.reward}</span>
                </div>
                <div class="order-body">
                    <h3>${order.package}</h3>
                    <div class="info-row"><i class="fas fa-map-marker-alt"></i> <span>${order.pickup}</span></div>
                    <div class="info-row"><i class="fas fa-door-open"></i> <span>送至: ${order.delivery}</span></div>
                    <div class="info-row"><i class="fas fa-user-circle"></i> <span>发布人: ${order.creator}</span></div>
                    ${order.worker ? `<div class="info-row"><i class="fas fa-hands-helping"></i> <span>接单人: ${order.worker}</span></div>` : ''}
                    ${order.frozen ? `<div class="info-row" style="color:#d97706;"><i class="fas fa-snowflake"></i> <span>纠纷冻结中</span></div>` : ''}
                </div>
                <div class="order-footer">
                    ${myOrdersView === 'accepted' && order.status === 'accepted' && !order.frozen ?
                    `<button class="btn-primary" onclick="updateStatus(${order.id}, 'delivered')">确认送达</button>` : ''}

                    ${myOrdersView === 'created' && (order.status === 'accepted' || order.status === 'delivered') && !order.frozen ?
                    `<button class="btn-primary" style="background:var(--accent);color:#fff" onclick="updateStatus(${order.id}, 'completed')">确认收货并支付</button>` : ''}

                    ${myOrdersView === 'created' && order.status === 'pending' ?
                    `<button class="btn-outline" style="color: #ef4444;" onclick="updateStatus(${order.id}, 'cancelled')"><i class="fas fa-undo"></i> 撤回发布</button>` : ''}

                    ${!order.frozen && (order.status === 'accepted' || order.status === 'delivered') ?
                    `<button class="btn-outline" style="color:var(--secondary);border-color:#fecdd3;" onclick="openDispute(${order.id}, '${myOrdersView === 'created' ? (order.worker || '') : order.creator}')"><i class="fas fa-gavel"></i> 发起纠纷</button>` : ''}

                    ${renderRatingEntry(order, isMyOrders)}
                </div>
            `;
            container.appendChild(card);
        });
    }

    // 已完成订单的互评入口：本方未评价则显示按钮，已评价则显示提示
    function renderRatingEntry(order, isMyOrders) {
        if (!isMyOrders || order.status !== 'completed') return '';
        const isCreatorView = myOrdersView === 'created';
        const alreadyRated = isCreatorView ? order.creatorRated : order.workerRated;
        const target = isCreatorView ? (order.worker || '') : order.creator;
        if (!target) return '';
        if (alreadyRated) {
            return '<div class="rating-done-tag"><i class="fas fa-check-circle"></i> 已评价</div>';
        }
        return `<button class="btn-primary" onclick="openRating(${order.id}, '${target}')"><i class="fas fa-star"></i> 评价对方</button>`;
    }

    // Filter Logic（驿站分类）
    elements.filterPills.forEach(pill => {
        pill.onclick = () => {
            elements.filterPills.forEach(p => p.classList.remove('active'));
            pill.classList.add('active');
            let ds = pill.dataset.filter;
            currentFilter = (ds === '全部') ? '' : ds;
            fetchOrders();
        };
    });

    // 信用筛选（全部 / 仅优良 / 低信用专区）—— 仅切换前端渲染，不刷新整站
    elements.creditFilterPills.forEach(pill => {
        pill.onclick = () => {
            elements.creditFilterPills.forEach(p => p.classList.remove('active'));
            pill.classList.add('active');
            creditFilter = pill.dataset.creditFilter;
            fetchOrders();
        };
    });

    // My Orders Toggle
    elements.showCreated.onclick = () => {
        elements.showCreated.classList.add('active');
        elements.showAccepted.classList.remove('active');
        myOrdersView = 'created';
        fetchMyOrders();
    };
    elements.showAccepted.onclick = () => {
        elements.showAccepted.classList.add('active');
        elements.showCreated.classList.remove('active');
        myOrdersView = 'accepted';
        fetchMyOrders();
    };

    // Post Order
    elements.orderForm.onsubmit = async (e) => {
        e.preventDefault();
        const payload = {
            package: document.getElementById('pkg-name').value,
            pickup: document.getElementById('pkg-pickup').value,
            delivery: document.getElementById('pkg-delivery').value,
            reward: document.getElementById('pkg-reward').value,
            creator: currentUser.username
        };
        try {
            const resp = await fetch('/api/orders', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (resp.ok) {
                showToast('发布成功！');
                elements.orderForm.reset();
                document.querySelector('[data-tab="dashboard"]').click();
            } else {
                showToast('发布失败');
            }
        } catch (err) { showToast('发布失败'); }
    }

    // Status Update
    window.updateStatus = async (id, status) => {
        const payload = { id, status, worker: currentUser.username };
        try {
            const resp = await fetch('/api/update_status', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (!resp.ok) {
                const data = await resp.json().catch(() => ({}));
                showToast((data && data.message) || '操作失败，请重试');
                return;
            }

            if (status === 'accepted') showToast('接单成功，请尽快送达！');
            else if (status === 'delivered') showToast('已送达，等待发单人确认。');
            else if (status === 'completed') showToast('任务完成，可在卡片上评价对方。');
            else if (status === 'cancelled') showToast('已成功撤回该订单。');

            if (document.getElementById('dashboard-tab').classList.contains('hidden')) fetchMyOrders();
            else fetchOrders();
        } catch (err) { showToast('操作失败'); }
    };

    // 评价 / 纠纷入口（委托给 CreditUI，刷新后回到我的订单列表）
    window.openRating = (orderId, target) => {
        CreditUI.openRatingModal(orderId, target, fetchMyOrders);
    };
    window.openDispute = (orderId, target) => {
        CreditUI.openDisputeModal(orderId, target, fetchMyOrders);
    };

    // --- Profile ---
    async function loadProfile() {
        elements.profileRealName.textContent = currentUser.realName;
        elements.profileMajor.textContent = currentUser.major;

        // Fetch real stats from backend
        try {
            // Count orders created by this user (发布任务)
            const createdResp = await fetch(`/api/orders?creator=${currentUser.username}`);
            const createdOrders = await createdResp.json();
            document.getElementById('stat-created').textContent = createdOrders.length;

            // Count orders completed as worker (代取成功)
            const workerResp = await fetch(`/api/orders?worker=${currentUser.username}`);
            const workerOrders = await workerResp.json();
            const completedCount = workerOrders.filter(o => o.status === 'completed').length;
            document.getElementById('stat-delivered').textContent = completedCount;

            // 信用分：从信用档案接口取真实值
            try {
                const credit = await CreditAPI.fetchCredit(currentUser.username, 'received');
                document.getElementById('stat-credit').textContent = credit.credit;
                currentUser.credit = credit.credit;
                localStorage.setItem('user', JSON.stringify(currentUser));
            } catch (e) {
                document.getElementById('stat-credit').textContent =
                    (typeof currentUser.credit === 'number') ? currentUser.credit : '100';
            }
        } catch (err) {
            console.error('Failed to load profile stats:', err);
        }
    }

    elements.editProfileBtn.onclick = () => {
        elements.editRealname.value = currentUser.realName;
        elements.editMajor.value = currentUser.major;
        elements.profileModal.classList.remove('hidden');
    };

    elements.accountSecBtn.onclick = () => {
        elements.editPassword.value = '';
        elements.securityModal.classList.remove('hidden');
    };

    elements.profileForm.onsubmit = async (e) => {
        e.preventDefault();
        const payload = {
            username: currentUser.username,
            realName: elements.editRealname.value,
            major: elements.editMajor.value
        };
        try {
            const resp = await fetch('/api/update_profile', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (resp.ok) {
                currentUser.realName = payload.realName;
                currentUser.major = payload.major;
                localStorage.setItem('user', JSON.stringify(currentUser));
                loadProfile();
                updateUIForLogin();
                elements.profileModal.classList.add('hidden');
                showToast('个人资料修改成功！');
            }
        } catch (err) { showToast('修改失败'); }
    };

    elements.securityForm.onsubmit = async (e) => {
        e.preventDefault();
        const payload = {
            username: currentUser.username,
            password: elements.editPassword.value
        };
        try {
            const resp = await fetch('/api/update_profile', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            if (resp.ok) {
                elements.securityModal.classList.add('hidden');
                showToast('新密码修改成功，请重新登录');
                setTimeout(() => elements.logoutBtn.click(), 1500);
            }
        } catch (err) { showToast('修改失败'); }
    };

    function showToast(msg) {
        const t = elements.toast;
        t.textContent = msg;
        t.classList.remove('hidden');
        setTimeout(() => t.classList.add('hidden'), 3000);
    }

    // 时间线跳转到关联订单：切到「我的订单」并高亮定位该订单卡片
    async function gotoOrder(orderId) {
        document.querySelector('[data-tab="my-orders"]').click();
        // 依次在「我发布的 / 我接单的」中查找该订单
        await fetchMyOrders();
        let card = highlightOrderCard(orderId);
        if (!card) {
            elements.showAccepted.click();
            await fetchMyOrders();
            highlightOrderCard(orderId);
        }
    }

    function highlightOrderCard(orderId) {
        const cards = elements.myOrdersList.querySelectorAll('.order-card');
        for (const c of cards) {
            if (c.dataset.orderId === String(orderId)) {
                c.scrollIntoView({ behavior: 'smooth', block: 'center' });
                c.classList.add('order-highlight');
                setTimeout(() => c.classList.remove('order-highlight'), 2200);
                return c;
            }
        }
        return null;
    }

    // 初始化信用分 + 纠纷仲裁模块 UI，注入共享上下文
    CreditUI.init({
        getUser: () => currentUser,
        toast: showToast,
        onCreditChanged: (credit) => {
            if (currentUser) {
                currentUser.credit = credit;
                localStorage.setItem('user', JSON.stringify(currentUser));
            }
        },
        gotoOrder: gotoOrder,
        openDisputeById: (id) => CreditUI.openDisputeById(id),
        acceptOrder: (id) => window.updateStatus(id, 'accepted')
    });

    // Init
    updateUIForLogin();
});
