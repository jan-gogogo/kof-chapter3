#include <kingofighter.hpp>

ACTION kingofighter::signup(name player) {
    //要求必须玩家本人注册
    require_auth(player);

    const uint32_t NOW_TS = current_time_point().sec_since_epoch();
    //实例化player表
    player_index player_tb(get_self(), get_self().value);
    //主键获取玩家的数据
    auto itr = player_tb.find(player.value);
    //如果玩家数据已存在，抛出异常
    check(itr == player_tb.end(), "player account exist!");
    //声明水晶数量1000个 乘10000是为了抵消0.0001
    const uint64_t amt = 1000 * 10000;
    //插入一条玩家数据
    player_tb.emplace(get_self(), [&](auto &r) {
        r.player_account = player;                            //玩家账号
        r.coin = asset(amt, symbol(symbol_code("SJ"), 4));    //初始水晶数量：1000
        r.counter = 0;                                        //玩家游戏局数
        r.created_at = NOW_TS;                                 //当前区块链时间
    });

    //实例化hero表
    //第二个入参(scope)为玩家账号
    hero_index hero_tb(get_self(), player.value);
    hero_tb.emplace(get_self(), [&](auto &r) {
        r.id = hero_tb.available_primary_key();
        r.hero_name = "jakiro";                               //英雄名称：杰奇诺
        r.min_atk = 35;                                       //攻击力最小值
        r.max_atk = 70;                                       //攻击力最大值
        r.hp = 500;                                           //血量值
        r.created_at = NOW_TS;                                 //当前区块链时间
    });
}

ACTION kingofighter::newgame(const name player,
                const string& user_seed,
                const checksum256& house_seed_hash,
                const uint64_t& expire_timestamp,
                const signature& sig){
    require_auth(player);

    game_index g_tb(get_self(), get_self().value);

    //1. 签名前的数据，格式：服务端种子哈希+时间戳
    string sig_ori_data = sha256_to_hex(house_seed_hash);
    sig_ori_data += uint64_string(expire_timestamp);
    
    //2. 校验house_seed_hash
    //防止重复提交相同的数据，判断house_seed_hash是否重复
    auto hsh_idx = g_tb.get_index<"byhsh"_n>();
    auto l_hsh_itr = hsh_idx.lower_bound(uint64_hash(house_seed_hash));
    bool hsh_exist = l_hsh_itr !=hsh_idx.end() && l_hsh_itr->house_seed_hash == house_seed_hash;
    check(!hsh_exist,"house seed hash duplicate");

    //3. 验签
    //声明服务端签名合约公钥，用于验签
    const public_key pub_key = str_to_pub("EOS7ikmSFnJ4UuAuGDPQMTZFBQa7Kh6QTzBAUivksFETmX6ncxGW7");
    const char *data_cstr = sig_ori_data.c_str();
    checksum256 digest = eosio::sha256(data_cstr, strlen(data_cstr));
    //必须是pub_key对应的私钥签名
    //如果不是，直接抛出异常
    eosio::assert_recover_key(digest,sig,pub_key);

    //4. 验签名的时间戳是否已过期
    const uint32_t NOW_TS = current_time_point().sec_since_epoch();
    check(expire_timestamp > NOW_TS, "house seed hash expired");

    //5. 保存数据
    g_tb.emplace(get_self(), [&](auto &r) {
        r.game_id = g_tb.available_primary_key();
        r.player_account = player;
        r.user_seed = user_seed;
        r.house_seed_hash = house_seed_hash;
        r.expire_timestamp = expire_timestamp;
        r.coin = asset(0, symbol(symbol_code("SJ"), 4));
        r.sig = sig;
        r.status = 1;
        r.created_at = NOW_TS;
    });
}

ACTION kingofighter::battle(const uint64_t& game_id,const string &house_seed) {
    require_auth(get_self());

    //1. 校验指定游戏id是否已被玩家提交
    game_index g_tb(get_self(), get_self().value);
    auto itr = g_tb.find(game_id);
    check(itr != g_tb.end(), "game does not exist");
    check(itr->status == 1,"invalid game status");

    //2. 校验服务端提交的hash(house_seed)是否就是玩家提交的house_seed_hash
    checksum256 house_seed_hash = itr->house_seed_hash;
    assert_sha256(house_seed.c_str(),strlen(house_seed.c_str()),house_seed_hash);

    //3. 使用house_seed和玩家提供的user_seed来生成本局对战的随机数
    //   格式：hash(house_seed + user_seed)
    //   最终是一个32位的uint8数组
    string seed_str = house_seed + itr->user_seed;
    const char *data_cstr = seed_str.c_str();
    checksum256 seed_hash = eosio::sha256(data_cstr, strlen(data_cstr));

    //4. 重置本局游戏的状态
     g_tb.modify(itr, _self, [&](auto &m) {
        m.status = 2; //已处理
    });
    
    //召唤玩家的英雄
    const name player = itr->player_account;
    player_index player_tb(get_self(), get_self().value);
    auto p_itr = player_tb.find(player.value);
    hero_index hero_tb(get_self(), player.value);
    const auto hero = hero_tb.begin();

    const uint32_t NOW_TS = current_time_point().sec_since_epoch();
    const uint32_t BOSS_MIN_ATK = 50;
    const uint32_t BOSS_MAX_ATK = 70;
    const uint32_t BOSS_HP = 700;
    uint32_t hero_hp = hero->hp;
    uint32_t boss_hp = BOSS_HP;

    vector <scoreboard> scoreboards;
    for (size_t i = 0; i < 32; i++) {
        const uint32_t hash_val = (uint32_t) seed_hash.extract_as_byte_array()[i] + NOW_TS;
        uint32_t damage;
        if (i & 1) {
            //i为奇数，BOSS攻击
            damage = hash_val % (BOSS_MAX_ATK - BOSS_MIN_ATK + 1) + BOSS_MIN_ATK;
            hero_hp = hero_hp > damage ? hero_hp - damage : 0;
        } else {
            //i为偶数,玩家攻击
            damage = hash_val % (hero->max_atk - hero->min_atk + 1) + hero->min_atk;
            //是否暴击，暴击概率25%
            if (hash_val % 4 == 0)
                damage += 100;
            boss_hp = boss_hp > damage ? boss_hp - damage : 0;
        }

        //这一轮的战斗结果
        scoreboard sb_item = {
                .round_no = i + 1,
                .attacker = i & 1 ? get_self() : player,
                .defender = i & 1 ? player : get_self(),
                .damage = damage,
                .defender_hp = i & 1 ? hero_hp : boss_hp
        };
        scoreboards.emplace_back(sb_item);
        //如果任何一方血量归0，战斗结束
        if (hero_hp == 0 || boss_hp == 0)
            break;
    }

    //修改玩家数据
    player_tb.modify(p_itr, _self, [&](auto &m) {
        m.counter += 1;
        //如果玩家输了，需要掉落100个水晶
        if (hero_hp == 0) {
            if (m.coin.amount <= 100 * 10000)
                m.coin = asset(0, symbol(symbol_code("SJ"), 4));
            else
                m.coin -= asset(100 * 10000, symbol(symbol_code("SJ"), 4));
        }
    });

    //记录下本次战斗的结果
    game_record_index gr_tb(get_self(), get_self().value);
    gr_tb.emplace(get_self(), [&](auto &r) {
        r.game_id = gr_tb.available_primary_key();
        r.player_account = player;
        r.player_counter = p_itr->counter;
        r.scoreboards = scoreboards;
        r.game_result = hero_hp > 0 ? "win" : "lose";
        r.created_at = NOW_TS;
    });

    //如果玩家赢了 随机奖励一个宝箱（金、银、铜）
    if (hero_hp > 0) {
        box_index box_tb(get_self(), get_self().value);
        box_tb.emplace(get_self(), [&](auto &r) {
            r.id = box_tb.available_primary_key();
            r.player_account = player;
            r.level = seed_hash.extract_as_byte_array()[31] % (uint8_t) 3 + 1;
            r.created_at = NOW_TS;
        });
    }
}

