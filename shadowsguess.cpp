#include <map>
#include <cmath>
#include <vector>
#include <eosiolib/types.hpp>
#include <eosiolib/time.hpp>
#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/transaction.hpp>
#include <boost/algorithm/string.hpp>

#define TEAM_TOKEN S(4, ESA)
#define CORE_TOKEN S(4, ESA)//EOS
#define CORE_ACCOUNT N(shadowbanker) //N(eosio.token)
#define TEAM_HOLDERS N(eosioshadows)
#define TEAM_ACCOUNT N(eosiodrizzle)
#define TEAM_SERVICE N(shadowserver)
#define TEAM_BANKERS N(shadowbanker)


using namespace std;
using namespace eosio;
using namespace boost;

class shadowsguess : public contract {

  public:

    const std::string TOKEN_LIST[10] = {"eos", "eth", "xrp", "bch", "ltc", "dash", "xmr", "iota","ada","neo"};

    struct betinfo{
      uint64_t play_type;
      std::vector<string> tokens;
      account_name referrer;
    };

    struct tokeninfo {
      std::string token;
      uint64_t price;
      std::string change_price;

      EOSLIB_SERIALIZE( tokeninfo, (token)(price)(change_price))
    };

    shadowsguess(account_name self):
    contract(self),
    games(_self, _self),
    rounds(_self, _self),
    items(_self, _self),
    players(_self, _self),
    bills(_self, _self)
    {
      initialize();
    }

    void transfer(const  account_name from, const account_name to, const asset quantity, const std::string memo ) 
    {
        require_auth( from ); 

        if(quantity.is_valid() && quantity.symbol == CORE_TOKEN && from != _self && to == _self)
        {
          if(quantity.amount==1)
          {
              withdraw(from);
          }else
          {
            auto gameitr = games.begin();
            eosio_assert( gameitr!=games.end(), "系统尚未创建游戏" );
            eosio_assert( gameitr->is_pause==0, "系统维护，请稍后再来" ); 
            eosio_assert( memo.size() <= 256, "备注信息不能超过256位" );
            eosio_assert( quantity.amount >= gameitr->min_bet, "单次竞猜数量不能低于0.1EOS" );
            eosio_assert( quantity.amount <= gameitr->max_bet, "单次竞猜数量不能超出10万EOS。" );

            auto round_number = now() - (now() % gameitr->close_offset);
            auto ritr = rounds.find(round_number);
            eosio_assert( ritr!=rounds.end(), "系统尚未创建场次" );

            auto now_time = time_point_sec(now());
            eosio_assert( now_time >= ritr->open_time, "尚未开盘，不能下单" );
            eosio_assert( now_time < ritr->close_time, "已经关盘，不能下单" );

            auto bet_info = get_bet_info(from, memo);
            auto bet_eos = quantity.amount;
            auto team_fee = bet_eos *  (double(gameitr->team_fee)/double(100));
            auto holder_fee = bet_eos *  (double(gameitr->holder_fee)/double(100));
            auto referrer_fee = bet_eos * (double(gameitr->referrer_fee)/double(100));
            auto bonus_fee = bet_eos * (double(gameitr->bonus_fee)/double(100));
            auto bet_amount = bet_eos - team_fee - holder_fee - referrer_fee - bonus_fee;
            auto bet_token = gameitr->is_miner==1?(bet_eos*10):0; //(gameitr->total_token_supply-gameitr->total_token_count) * bet_eos / (gameitr->total_token_base+gameitr->total_bet_amount+bet_eos);
            if(bet_token>(gameitr->total_token_supply-gameitr->total_token_count))bet_token=0;

            //写入数据
            
            auto is_new_player = false;
            auto playeritr = players.find(from);
            if(playeritr == players.end())
            {
              is_new_player=true;
              playeritr = players.emplace(_self,[&](auto & s){
                s.owner = from;
                s.referrer = bet_info.referrer;
                s.bet_amount = bet_eos;
                s.token_count = bet_token;
              });
            }else{
              is_new_player=false;
              players.modify(playeritr, 0, [&](auto &s) {
                s.bet_amount += bet_eos;
                s.token_count += bet_token;
              });
            }
            auto every_token = bet_amount / bet_info.tokens.size();
            for(auto &tokenitem:bet_info.tokens)
            {
              eosio_assert( tokenitem=="eos" || tokenitem=="eth" || tokenitem=="xrp" || tokenitem=="bch" || tokenitem=="ltc" || tokenitem=="dash" || tokenitem=="xmr" || tokenitem=="iota" || tokenitem=="ada" || tokenitem=="neo", "输入的币种不存在。" );
              auto token = string_to_name(tokenitem.c_str());
              auto itemidx = items.template get_index<N(tuple)>();
              auto itemitr = itemidx.lower_bound(item::key(ritr->id,token));
              if(itemitr!=itemidx.end() && itemitr->round==ritr->id && itemitr->token==token) 
              {
                itemidx.modify(itemitr, 0, [&](auto &s) 
                {
                  s.first_pool += ( bet_info.play_type==1 ? every_token : 0 );
                  s.place_pool += ( bet_info.play_type==2 ? every_token : 0);
                  s.last_pool += ( bet_info.play_type==3 ? every_token : 0 );
                });
              }

              auto bidx = bills.template get_index<N(bet)>();
              auto bitr = bidx.lower_bound(bill::betkey(ritr->id,from,bet_info.play_type,token));
              if(bitr!=bidx.end() && bitr->round==ritr->id && bitr->owner==from && bitr->play_type==bet_info.play_type && bitr->token==token)
              {
                bidx.modify(bitr, 0, [&](auto &s) 
                {
                  s.bet_amount += every_token;
                  s.bet_time = now_time;
                });
              }else
              {
                rounds.modify(ritr,0,[&](auto &s){
                  s.bill_count += 1;
                }); 
                games.modify(gameitr, 0, [&](auto &s) {
                  s.total_bill_count += 1;
                });
                bills.emplace(_self,[&](auto & s){
                  s.id = ritr->id + gameitr->total_bill_count;
                  s.token = token;
                  s.round = ritr->id;
                  s.owner = from;
                  s.play_type = bet_info.play_type;
                  s.bet_amount = every_token;
                  s.bet_time = now_time;
                });

              }
            }


            //推荐分红
            auto agent = players.find(playeritr->referrer);
            if(agent != players.end() )
            {
                players.modify( agent,0, [&]( auto& s ) {
                    s.referrer_fee += referrer_fee;
                });
            }

            games.modify(gameitr, 0, [&](auto &s) {
              s.total_user_count += (is_new_player?1:0);
              s.total_token_count += bet_token;
              s.total_bet_amount += bet_amount;
              s.total_team_fee +=team_fee;
              s.total_holder_fee +=holder_fee;
              s.total_referrer_fee+= referrer_fee;
              s.total_bonus_fee += bonus_fee;
            });

            rounds.modify(ritr,0,[&](auto &s){
              s.first_pool += ( bet_info.play_type==1 ? bet_amount : 0 );
              s.place_pool += ( bet_info.play_type==2 ? bet_amount : 0);
              s.last_pool += ( bet_info.play_type==3 ? bet_amount : 0 );
              s.team_fee +=team_fee;
              s.bonus_fee += bonus_fee;
              s.holder_fee +=holder_fee;
              s.referrer_fee+=referrer_fee;
            });
          }
            
        }
    }
    
    //@abi action
    void withdraw(const account_name from)
    {
      require_auth( from ); 

      auto gameitr = games.begin();
      eosio_assert( gameitr->is_pause==0, "系统维护，请稍后再来" );

      auto playeritr = players.find( from );
      eosio_assert( playeritr != players.end(), "账号不存在");
      eosio_assert( (playeritr->balance+playeritr->referrer_fee) >= 100, "提取的利润不足0.01EOS");

      asset bettoken(playeritr->token_count,TEAM_TOKEN);
      asset balance((playeritr->balance+playeritr->referrer_fee),CORE_TOKEN);

      players.modify( playeritr, 0, [&]( auto& s ) {
          s.balance = 0;
          s.token_count=0;
          s.referrer_fee=0;
      });

      action(
      permission_level{ _self, N(active) },
      CORE_ACCOUNT, N(transfer),
      std::make_tuple(_self,from, balance, std::string("影马是EOS上绝对公平的赛马游戏，采用香港马会的浮动赔率算法，使用第三方（https://coinmarketcap.com）数据开奖，每笔交易可查可追溯，玩家与玩家之间的博弈：http://eoshorse.io"))
      ).send();

      if(gameitr->is_miner==1 && bettoken.amount>0)
      {
        action(
        permission_level{ _self, N(active) },
        TEAM_BANKERS, N(transfer),
        std::make_tuple(_self,from, bettoken, std::string("影马是EOS上绝对公平的赛马游戏，采用香港马会的浮动赔率算法，使用第三方（https://coinmarketcap.com）数据开奖，每笔交易可查可追溯，玩家与玩家之间的博弈：http://eoshorse.io"))
        ).send();
      }
    }

    //@abi action
    void clean(const account_name from,uint64_t clean_type,uint64_t start,uint64_t stop)
    {
      require_auth( from ); 
      eosio_assert( from==TEAM_SERVICE, "你没有权限执行此操作");

      if(clean_type==1)//游戏
      {
        auto gameitr = games.begin();
        while( gameitr != games.end() ) {gameitr = games.erase(gameitr);}  
      }else if(clean_type==2)//场次
      {
        auto rounditr = rounds.lower_bound(start);
        auto roundcounter = 0;
        while( rounditr != rounds.end() && rounditr->id<stop ) {
          if(roundcounter>=200)break;
          rounditr=rounds.erase(rounditr);
          roundcounter++;
        }  
      }else if(clean_type==3)//账单
      {
        auto billidx = bills.template get_index<N(round)>();
        auto billitr = billidx.lower_bound(start);
        auto billcounter = 0;
        while( billitr != billidx.end() && billitr->round<stop ) {
          if(billcounter>=200)break;
          billitr = billidx.erase(billitr);
          billcounter++;
        }
      }else if(clean_type==4)//币种
      {
        auto itemidx = items.template get_index<N(round)>();
        auto itemitr = itemidx.lower_bound(start);
        auto itemcounter = 0;
        while( itemitr != itemidx.end() && itemitr->round<stop ) {
          if(itemcounter>=200)break;
          itemitr = itemidx.erase(itemitr);
          itemcounter++;
        }
      }else if(clean_type==5)//玩家
      {
        auto playeritr = players.begin();
        auto playercounter = 0;
        while( playeritr != players.end() ) {
          if(playercounter>=200)break;
          playeritr = players.erase(playeritr);
          playercounter++;
        }
      }
    }

    //@abi action
    void settletoken(const account_name from, const uint64_t round)
    {
      require_auth( from ); 
      eosio_assert( from==TEAM_SERVICE, "你没有权限执行此操作");

      auto now_time = time_point_sec(now());
      auto gameitr = games.begin();
      eosio_assert( gameitr->is_pause==0, "系统维护，请稍后再来" );

      auto ritr  = rounds.find(round);
      eosio_assert( ritr!=rounds.end(), "结算的场次不存在");
      eosio_assert( ritr->round_status==2 && now_time >= ritr->draw_time, "场次不满足结算条件");

      std::map<account_name,double> firstlist;
      std::map<account_name,double> placelist;
      std::map<account_name,double> lastlist;

      auto itemidx = items.template get_index<N(round)>();
      auto itemitr = itemidx.lower_bound(ritr->id);
      while(itemitr!=itemidx.end() && itemitr->round==ritr->id)
      {
        firstlist[itemitr->token]= itemitr->first_pool>0? (double(ritr->first_pool)) / double(itemitr->first_pool):double(0);
        placelist[itemitr->token]= itemitr->place_pool>0? (double(ritr->place_pool)) / double(itemitr->place_pool) / double(3.0):double(0);
        lastlist[itemitr->token]=  itemitr->last_pool>0? (double(ritr->last_pool)) / double(itemitr->last_pool):double(0);
        itemitr++;
      }

      auto bidx = bills.template get_index<N(draw)>();
      auto bitr = bidx.lower_bound(bill::drawkey(ritr->id,0));
      std::map<uint64_t,uint64_t> winnerlist;
      while(bitr!=bidx.end() && bitr->round==ritr->id && bitr->is_draw==0)
      {
        if(winnerlist.size()>gameitr->settle_batch) break;

        auto win_amount = 0UL;
        if(bitr->play_type==1 && bitr->token==ritr->first_place)
        {
          win_amount += bitr->bet_amount * firstlist[bitr->token];
        }else if(bitr->play_type==2 && (bitr->token==ritr->first_place || bitr->token==ritr->second_place || bitr->token==ritr->third_place))
        {
          win_amount += bitr->bet_amount * placelist[bitr->token];
        }else if(bitr->play_type==3 && bitr->token==ritr->last_place)
        {
          win_amount += bitr->bet_amount * lastlist[bitr->token];
        }

        winnerlist.insert(pair<uint64_t, uint64_t>(bitr->id, win_amount));
        bitr++;
      }
      
      for(const auto& winner : winnerlist )
      {
        auto biditr = bills.find(winner.first);
        if(biditr!=bills.end())
        {
          bills.modify( biditr,0, [&]( auto& s ) {
            s.is_draw = 1;
            s.win_amount = winner.second;
          });

          auto oweritr = players.find(biditr->owner);
          if(oweritr!=players.end())
          {
            players.modify(oweritr, 0, [&](auto &s) {
              s.balance += winner.second;
              s.profit += winner.second;
            });
          }

          rounds.modify(ritr, 0, [&](auto &s) {
            s.first_settle += biditr->play_type==1? winner.second:0;
            s.place_settle += biditr->play_type==2? winner.second:0;
            s.last_settle += biditr->play_type==3? winner.second:0;
            s.settle_count+=1;
          });
        }
      }

      if(ritr->settle_count>=ritr->bill_count && ritr!=rounds.end())
      {
        if(ritr->first_pool>ritr->first_settle && (ritr->first_pool-ritr->first_settle)>0)
        {
          auto first_left_fee = ritr->first_pool-ritr->first_settle;
          auto first_team_fee = first_left_fee * 0.5;
          auto first_holder_fee = first_left_fee - first_team_fee;
          rounds.modify(ritr, 0, [&](auto &s) {
            s.team_fee += first_team_fee;
            s.holder_fee += first_holder_fee;
            s.first_settle += first_left_fee;
          });

          games.modify(gameitr,0,[&](auto &s){
            s.total_team_fee += first_team_fee;
            s.total_holder_fee += first_holder_fee;
          });
        }

        if(ritr->place_pool>ritr->place_settle && (ritr->place_pool-ritr->place_settle)>0)
        {
          auto place_left_fee = ritr->place_pool-ritr->place_settle;
          auto place_team_fee = place_left_fee * 0.5;
          auto place_holder_fee = place_left_fee - place_team_fee;
          rounds.modify(ritr, 0, [&](auto &s) {
            s.team_fee += place_team_fee;
            s.holder_fee += place_holder_fee;
            s.place_settle += place_left_fee;
          });

          games.modify(gameitr,0,[&](auto &s){
            s.total_team_fee += place_team_fee;
            s.total_holder_fee += place_holder_fee;
          });
        }

        if(ritr->last_pool>ritr->last_settle && (ritr->last_pool-ritr->last_settle)>0)
        {
          auto last_left_fee = ritr->last_pool-ritr->last_settle;
          auto last_team_fee = last_left_fee * 0.5;
          auto last_holder_fee = last_left_fee - last_team_fee;
          rounds.modify(ritr, 0, [&](auto &s) {
            s.team_fee += last_team_fee;
            s.holder_fee += last_holder_fee;
            s.last_settle += last_left_fee;
          });

          games.modify(gameitr,0,[&](auto &s){
            s.total_team_fee += last_team_fee;
            s.total_holder_fee += last_holder_fee;
          });
        }

        auto teamitr = players.find( TEAM_ACCOUNT );
        if( teamitr != players.end())
        {
          players.modify(teamitr,0,[&](auto &s){
            s.balance+=ritr->team_fee;
          });
        }

        rounds.modify( ritr,0, [&]( auto& s ) {
            s.round_status = 3;
        });

        if(ritr->holder_fee>0)
        {
          asset balance(ritr->holder_fee,CORE_TOKEN);
          transaction out; 
          out.actions.emplace_back(permission_level{_self, N(active)}, TEAM_BANKERS, N(transfer), std::make_tuple(_self,TEAM_HOLDERS, balance, std::string("影马是EOS上绝对公平的赛马游戏，采用香港马会的浮动赔率算法，使用第三方（https://coinmarketcap.com）数据开奖，每笔交易可查可追溯，玩家与玩家之间的博弈：http://eoshorse.io"))); 
          out.delay_sec = 5; 
          out.send(next_id(), _self, false); 
        }


      }
    }

    //@abi action
    void drawtoken(const account_name from,const uint64_t round,const uint64_t status,const std::vector<tokeninfo>& tokens,const std::vector<string>& winners)
    {
      require_auth( from ); 
      eosio_assert( from==TEAM_SERVICE, "你没有权限执行此操作");

      auto gameitr = games.begin();
      eosio_assert( gameitr->is_pause==0, "系统维护，请稍后再来" );

      auto now_time = time_point_sec(now());
      auto ritr = rounds.find(round);
      eosio_assert( tokens.size()>0, "货币信息不正确");
      eosio_assert( ritr!=rounds.end(), "场次信息不正确");
      eosio_assert( status==1 || status==2, "开奖参数错误" );
      if(status ==1)
      {
        eosio_assert( now_time >= ritr->close_time, "尚未到达关盘时间" );
        rounds.modify(ritr, 0, [&](auto &s) {s.round_status=1;});
      }else if(status ==2)
      {
        eosio_assert( winners.size()>=4, "中奖货币不正确");
        eosio_assert( now_time >= ritr->draw_time, "尚未到达开奖时间" );
        rounds.modify(ritr, 0, [&](auto &s) {
        s.round_status = 2;
        s.first_place = string_to_name(winners[0].c_str());
        s.second_place = string_to_name(winners[1].c_str());
        s.third_place = string_to_name(winners[2].c_str());
        s.last_place = string_to_name(winners[winners.size()-1].c_str());
        });
      }

      for(const auto&info:tokens)
      {
        auto token = string_to_name(info.token.c_str());
        auto itemidx = items.template get_index<N(tuple)>();
        auto itemitr = itemidx.lower_bound(item::key(ritr->id,token));
        if(itemitr!=itemidx.end() && itemitr->round==ritr->id && itemitr->token==token) 
        {
          itemidx.modify(itemitr, 0, [&](auto &s) 
          {
            if(status==1){
              s.start_price = info.price;
            }else if(status ==2){
              s.stop_price = info.price;
              s.change_price = stol(info.change_price);
            }
          });
        }
      }
    }

    //@abi action
    void interval(const account_name from)
    {
      require_auth( from ); 
      eosio_assert( from==TEAM_SERVICE, "你没有权限执行此操作");

      auto gameitr = games.begin();
      if(gameitr==games.end())
      {
        initialize();
      }
      gameitr = games.begin();

      auto timestamp = now();
      auto period = gameitr->close_offset;
      auto whole_day = timestamp + period*gameitr->max_round;
      auto rounditr = rounds.rbegin();
      auto counter = 0UL;
      if(rounditr!=rounds.rend())
      {
        counter = rounditr->round_number;
      }
      
      for(auto i=timestamp;i<whole_day;i++)
      {
        if(i % period !=0 || rounds.find(i)!=rounds.end()) continue;
        counter++;
        rounds.emplace(_self,[&](auto & s){
          s.id = i;
          s.round_number = counter;
          s.open_time = time_point_sec(i);
          s.close_time =time_point_sec(i+gameitr->close_offset-gameitr->open_offset);
          s.draw_time = time_point_sec(i+gameitr->draw_offset); 
        });

        for(auto j=0;j<10;j++)
        {
          items.emplace(_self,[&](auto & s){
            s.id = i+j;
            s.round = i;
            s.token = string_to_name(TOKEN_LIST[j].c_str());
          });
        }

      }
    }

    //@abi action
    void setpause(const account_name from)
    {
      require_auth( from ); 
      eosio_assert( from==TEAM_SERVICE, "你没有权限执行此操作");
      auto gameitr = games.begin();
      games.modify(gameitr, 0, [&](auto &s) {
        s.is_pause = gameitr->is_pause==0?1:0;
      });
    }

    //@abi action
    void setlimit(const account_name from,const uint64_t min_bet,const uint64_t max_bet,const uint64_t max_round,const uint64_t is_miner)
    {
      require_auth( from ); 
      eosio_assert( from==TEAM_SERVICE, "你没有权限执行此操作");
      auto gameitr = games.begin();
      games.modify(gameitr, 0, [&](auto &s) {
        s.min_bet = min_bet*10000;
        s.max_bet = max_bet*10000;
        s.max_round = max_round;
        s.is_miner = is_miner;
      });
    }

    //@abi action
    void setoffset(const account_name from,const uint64_t open_offset,const uint64_t close_offset,const uint64_t draw_offset)
    {
      require_auth( from ); 
      eosio_assert( from==TEAM_SERVICE, "你没有权限执行此操作");
      auto gameitr = games.begin();
      games.modify(gameitr, 0, [&](auto &s) {
        s.open_offset = open_offset;
        s.draw_offset = draw_offset;
        s.close_offset = close_offset;
      });
    }

    void initialize()
    {
      auto gameitr = games.begin();
      if(gameitr==games.end())
      {
        games.emplace(_self,[&](auto & s){
          s.id = 0;
          s.is_pause = 0;                        // 临时封盘
          s.is_miner = 0;                        // 是否允许挖矿
          s.min_bet = 1000;                      // 最低下单额0.1EOS
          s.max_bet = 1000000000;                // 最大下下单金额10万EOS
          s.max_round = 10;                      // 最大创建场次数
          s.team_fee = 2;                        // 团队手续费
          s.bonus_fee = 0;                      // 分红比例
          s.holder_fee = 2;                      // 简影手续费
          s.referrer_fee = 1;                    // 推荐手续费
          s.open_offset =  10;                   // 开盘间隔
          s.close_offset = 5*60;                 // 关盘间隔
          s.draw_offset = 10*60;                  // 开奖间隔
          s.settle_batch = 100;                  // 分批结算数量
          s.total_token_base = 5000000000;       // 初始底仓50万EOS
          s.total_token_supply = 50000000000;    // 初始500万简影币（ESA）
          s.transaction_id = 1;                  // 异步交易ID
        });
      }
    }

    betinfo get_bet_info(const account_name from,std::string memo)
    {
      betinfo result;

      eosio_assert( memo.size()>0, "请在Memo种输入竞猜玩法及币种信息" );

      memo.erase(std::remove_if(memo.begin(),memo.end(),[](unsigned char x) { return std::isspace(x); }),memo.end());

      auto teamitr = players.find( TEAM_ACCOUNT );
      if( teamitr == players.end())
      {
          players.emplace( _self, [&]( auto& s ) {s.owner = TEAM_ACCOUNT;});
      }

      auto bet_info = memo;
      auto separator = memo.find('-');
      if (separator != string::npos) {
          bet_info = memo.substr(0, separator);
          auto referrer_info = memo.substr(separator + 1);
          auto referrer_account = string_to_name(referrer_info.c_str());
          if(players.find(referrer_account)==players.end() || referrer_info.size()<=0 || referrer_info.size()>12 || referrer_account==_self || from==referrer_account)
          {
            result.referrer = TEAM_ACCOUNT;
          }else
          {
            result.referrer = referrer_account;
          }
      } else {
          result.referrer = TEAM_ACCOUNT;
      }

      separator = bet_info.find('=');
      eosio_assert( separator != string::npos, "竞猜信息缺少玩法分隔符号" );
      std::string type_name = bet_info.substr(0,separator);
      eosio_assert( type_name=="1" || type_name=="2" || type_name=="3", "竞猜玩法必须是数字：1（涨幅第一）、2（涨幅前三）、3（跌幅第一）" );
      result.play_type = atoll(type_name.c_str());
      std::string token_name = bet_info.substr(separator+1);
      eosio_assert( token_name.size()>0 && token_name.size()<50, "币种名称不正确" );
      std::transform(token_name.begin(), token_name.end(), token_name.begin(), ::tolower);
      vector<string> fields;
      split(fields, token_name, is_any_of( "," ) );
      result.tokens = fields;

      return result;
    }



    uint64_t get_token(const std::string token_name)
    {
      for(auto j=0;j<10;j++)
      {
        if(TOKEN_LIST[j]==token_name)
        {
          return j;
        }
      }
      return 999;
    }

    uint64_t next_id(){
        auto gameitr = games.begin();
        games.modify(gameitr, 0, [&](auto &s) {
            s.transaction_id++;
        });
        return gameitr->transaction_id;
    }

  private:

    // @abi table games i64
    struct game{
      uint64_t id;                    // id
      uint64_t total_user_count;      // 总用户数量
      uint64_t total_bill_count;      // 总注单数量
      uint64_t total_token_count;     // 总简影币发行量
      uint64_t total_bet_amount;      // 总投注金额
      uint64_t total_team_fee;        // 总研发手续费
      uint64_t total_bonus_fee;       // 总分红数量
      uint64_t total_holder_fee;      // 总简影股东分红
      uint64_t total_referrer_fee;    // 总推荐人分红
      uint64_t total_token_base;      // 简影币底仓
      uint64_t total_token_supply;    // 简影币供应量
      uint64_t is_pause;              // 紧急封盘
      uint64_t is_miner;              // 是否允许挖矿
      uint64_t is_daytime;            // 是否白天
      uint64_t min_bet;               // 最低下注
      uint64_t max_bet;               // 最高下注
      uint64_t max_round;             // 最大场次
      uint64_t open_offset;           // 开盘间隔
      uint64_t draw_offset;           // 开奖间隔
      uint64_t close_offset;          // 关盘间隔
      uint64_t team_fee;              // 系统手续费
      uint64_t bonus_fee;             // 分红比例
      uint64_t holder_fee;            // 系统手续费
      uint64_t referrer_fee;          // 推荐人手续费
      uint64_t settle_batch;          // 分批结算数量
      uint64_t transaction_id;        // 延迟交易id

      uint64_t primary_key() const { return id; }
      EOSLIB_SERIALIZE(game, (id)(total_user_count)(total_bill_count)(total_token_count)(total_bet_amount)(total_team_fee)(total_bonus_fee)(total_holder_fee)(total_referrer_fee)(total_token_base)(total_token_supply)(is_pause)(is_miner)(is_daytime)(min_bet)(max_bet)(max_round)(open_offset)(draw_offset)(close_offset)(team_fee)(bonus_fee)(holder_fee)(referrer_fee)(settle_batch)(transaction_id))
    };
    typedef multi_index<N(games), game> _game;
    _game games;

    // @abi table rounds i64
    struct round{
      uint64_t id;                    // id
      uint64_t round_status;          // 场次状态：0=待关盘，1=已关盘，2=已开奖，3=已结算
      uint64_t round_number;          // 当前场次
      time_point_sec open_time;       // 开盘时间
      time_point_sec close_time;      // 关盘时间
      time_point_sec draw_time;       // 开奖时间
      uint64_t first_pool;            // 冠绝奖池
      uint64_t first_settle;          // 冠绝奖池
      uint64_t place_pool;            // 位置奖池
      uint64_t place_settle;          // 位置奖池
      uint64_t last_pool;             // 幕落奖池
      uint64_t last_settle;           // 幕落奖池
      account_name first_place;         // 涨幅第一名
      account_name second_place;        // 涨幅第二名
      account_name third_place;         // 涨幅第三名
      account_name last_place;          // 跌幅末一名
      uint64_t team_fee;              // 总研发手续费
      uint64_t bonus_fee;             // 分红比例
      uint64_t holder_fee;            // 总简影股东分红
      uint64_t referrer_fee;          // 总推荐人分红
      uint64_t bill_count;            // 账单数量
      uint64_t settle_count;          // 结算数量

      uint64_t primary_key() const { return id; }
      EOSLIB_SERIALIZE(round, (id)(round_status)(round_number)(open_time)(close_time)(draw_time)(first_pool)(first_settle)(place_pool)(place_settle)(last_pool)(last_settle)(first_place)(second_place)(third_place)(last_place)(team_fee)(bonus_fee)(holder_fee)(referrer_fee)(bill_count)(settle_count))
    };
    typedef multi_index<N(rounds), round
    > _round;
    _round rounds;

    // @abi table items i64
    struct item {
      uint64_t id;                    // id
      uint64_t round;                 // 当前场次
      account_name token;               // 货币名称
      uint64_t first_pool;            // 冠绝金额
      uint64_t place_pool;            // 位置金额
      uint64_t last_pool;             // 幕落金额
      uint64_t start_price;           // 开盘价格
      uint64_t stop_price;            // 关盘价格
      int64_t  change_price;          // 涨跌幅度

      static key256 key(uint64_t round, uint64_t token) 
      {
         return key256::make_from_word_sequence<uint64_t>(round, token);
      }

      uint64_t primary_key() const { return id; }
      uint64_t get_round() const { return round; }
      key256 get_key() const { return key(round, token); }

      EOSLIB_SERIALIZE(item, (id)(round)(token)(first_pool)(place_pool)(last_pool)(start_price)(stop_price)(change_price))
    };
    typedef multi_index<N(items), item,
    indexed_by<N(round), const_mem_fun<item, uint64_t, &item::get_round>>,
    indexed_by<N(tuple), const_mem_fun<item, key256, &item::get_key>>
    > _item;
    _item items;

    // @abi table bills i64
    struct bill{
      uint64_t id;                   // id
      uint64_t round;                // 场次
      account_name token;              // 货币
      uint64_t is_draw;              // 是否结算
      account_name owner;            // 玩家账号
      uint64_t play_type;            // 玩法类型
      uint64_t bet_amount;           // 下注金额
      uint64_t win_amount;           // 获得利润
      time_point_sec bet_time;       // 竞猜时间

      static key256 drawkey(uint64_t round,uint64_t is_draw) 
      {
         return key256::make_from_word_sequence<uint64_t>(round,is_draw);
      }

      static key256 betkey(uint64_t round, uint64_t owner, uint64_t play_type, uint64_t token) 
      {
         return key256::make_from_word_sequence<uint64_t>(round, owner, play_type, token);
      }

      uint64_t primary_key() const { return id; }
      uint64_t get_round_key() const { return round; }
      key256 get_draw_key() const { return drawkey(round,is_draw); }
      key256 get_bet_key() const { return betkey(round, owner, play_type, token); }

      EOSLIB_SERIALIZE(bill, (id)(round)(token)(is_draw)(owner)(play_type)(bet_amount)(win_amount)(bet_time))
    };
    typedef multi_index<N(bills), bill,
    indexed_by<N(round), const_mem_fun<bill, uint64_t, &bill::get_round_key>>,
    indexed_by<N(draw), const_mem_fun<bill, key256, &bill::get_draw_key>>,
    indexed_by<N(bet), const_mem_fun<bill, key256, &bill::get_bet_key>>
    > _bill;
    _bill bills;
    
    // @abi table players i64
    struct player {
      account_name owner;           // 玩家账号
      account_name referrer;        // 推荐人
      uint64_t balance;             // 账户余额
      uint64_t profit;              // 总获利数量
      uint64_t bet_amount;          // 总投入数量
      uint64_t token_count;         // 简影矿产
      uint64_t referrer_fee;        // 推荐奖励

      account_name primary_key() const { return owner; }
      EOSLIB_SERIALIZE(player, (owner)(referrer)(balance)(profit)(bet_amount)(token_count)(referrer_fee))
    };
    typedef multi_index<N(players), player
    > _player;
    _player players;

};

 #define EOSIO_ABI_EX( TYPE, MEMBERS ) \
 extern "C" { \
    void apply( uint64_t receiver, uint64_t code, uint64_t action ) { \
       if( action == N(onerror)) { \
          eosio_assert(code == N(eosio), "onerror action's are only valid from the \"eosio\" system account"); \
       } \
       auto self = receiver; \
       if((code == CORE_ACCOUNT && action == N(transfer)) || (code == self && (action == N(withdraw) || action == N(drawtoken) || action == N(settletoken) || action == N(interval) || action == N(setoffset) || action == N(setlimit) || action == N(setpause) || action == N(clean) || action == N(onerror))) ) { \
          TYPE thiscontract( self ); \
          switch( action ) { \
             EOSIO_API( TYPE, MEMBERS ) \
          } \
       } \
    } \
 }

EOSIO_ABI_EX(shadowsguess, (transfer)(withdraw)(drawtoken)(settletoken)(setoffset)(setlimit)(setpause)(interval)(clean))